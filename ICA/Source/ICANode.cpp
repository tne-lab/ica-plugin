/*
------------------------------------------------------------------
This file is part of a plugin for the Open Ephys GUI
Copyright (C) 2019 Translational NeuroEngineering Laboratory
------------------------------------------------------------------
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ICANode.h"
#include "ICAEditor.h"

#include <iostream>
#include <utility>

using namespace ICA;

/*****  ICANode *****/

// static members
const float ICANode::icaTargetFs    (500.0f);

const String ICANode::chanHintPrefix("!chans: ");

const String ICANode::inputFilename("input.floatdata");
const String ICANode::configFilename("binica.sc");
const String ICANode::weightFilename("output.wts");
const String ICANode::sphereFilename("output.sph");
const String ICANode::mixingFilename("output.mix");
const String ICANode::unmixingFilename("output.unmix");

ICANode::ICANode()
    : GenericProcessor  ("ICA")
    , Thread            ("ICA Computation")
    , icaSamples        (int(icaTargetFs * 240))
    , componentBuffer   (16, 1024)
    , currSubProc       (0)
    , icaRunning        (var(false))
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
}

ICANode::~ICANode()
{
    if (isThreadRunning())
    {
        stopThread(500);
    }
}

AudioProcessorEditor* ICANode::createEditor()
{
    editor = new ICAEditor(this);
    return editor;
}

bool ICANode::startICA()
{
    if (isThreadRunning())
    {
        return false;
    }

    startThread();
    return true;
}

void ICANode::resetICA(uint32 subProc, bool block)
{
    auto dataEntry = subProcData.find(subProc);
    if (dataEntry != subProcData.end())
    {
        SubProcData& data = dataEntry->second;

        ScopedPointer<ScopedWriteLock> blockingLock;
        ScopedPointer<ScopedWriteTryLock> tryLock;

        if (block)
        {
            blockingLock = new ScopedWriteLock(data.icaMutex);
        }
        else
        {
            tryLock = new ScopedWriteTryLock(data.icaMutex);
            if (!tryLock->isLocked())
            {
                // uh-oh, if it's being written to now, we don't want to
                // throw the new thing away
                // bail out
                return;
            }
        }

        data.icaOp = new ICAOperation();
        data.icaConfigPath = "";
    }
}

Result ICANode::loadICA(const File& configFile)
{
    return loadICA(configFile, currSubProc);
}

void ICANode::resetCache(uint32 subProc)
{
    auto dataEntry = subProcData.find(subProc);
    if (dataEntry != subProcData.end())
    {
        AudioBufferFifo::LockHandle bufHandle(*dataEntry->second.dataCache);
        bufHandle.reset();
    }
}

bool ICANode::disable()
{
    // clear data caches
    for (auto& subProcEntry : subProcData)
    {
        AudioBufferFifo& dataCache = *subProcEntry.second.dataCache;
        AudioBufferFifo::LockHandle hCache(dataCache);
        hCache.reset();
    }

    return true;
}

void ICANode::process(AudioSampleBuffer& buffer)
{
    // should only do anything on the first buffer, since "buffer" 
    // should always be the same length during acquisition
    componentBuffer.setSize(componentBuffer.getNumChannels(), buffer.getNumSamples(), false, false, true);

    // process each subprocessor individually
    for (auto& subProcEntry : subProcData)
    {
        SubProcData& data = subProcEntry.second;

        jassert(data.channelInds.size() > 0);
        int nSamps = getNumSamples(data.channelInds[0]);

        // add data to cache, if possible
        AudioBufferFifo::TryLockHandle hCache(*data.dataCache);

        if (hCache.isLocked())
        {
            int s;
            for (s = data.dsOffset; s < nSamps; s += data.dsStride)
            {
                hCache.copySample(buffer, data.channelInds, s);
            }

            data.dsOffset = s - nSamps;
        }

        // do ICA!
        const ScopedReadTryLock icaOpLock(data.icaMutex);
        if (!icaOpLock.isLocked() || data.icaOp->isNoop())
        {
            continue;
        }

        const ICAOperation& op = *data.icaOp;
        
        const SortedSet<int>& icaChansRel = op.enabledChannels;
        int nChans = icaChansRel.size(); // also number of total components

        // whether to start from 0 and add components or start from all and subtract them
        bool additive = op.rejectedComponents.size() > nChans / 2;
        
        SortedSet<int> comps;
        if (additive)
        {
            for (int i = 0; i < nChans; ++i)
            {
                comps.add(i);
            }

            comps.removeValuesIn(op.rejectedComponents);
        }
        else
        {
            comps = op.rejectedComponents;
        }

        int nComps = comps.size();

        for (int comp : comps)
        {
            componentBuffer.clear(comp, 0, nSamps);

            // unmix into the components
            for (int kChan = 0; kChan < nChans; ++kChan)
            {
                int chan = data.channelInds[icaChansRel[kChan]];

                componentBuffer.addFrom(comp, 0, buffer, chan, 0, nSamps, op.unmixing(comp, kChan));
            }
        }

        if (additive)
        {
            // clear channels before adding components we want to keep
            for (int kChan = 0; kChan < nChans; ++kChan)
            {
                buffer.clear(data.channelInds[icaChansRel[kChan]], 0, nSamps);
            }
        }

        for (int comp : comps)
        {
            // remix back into channels
            for (int kChan = 0; kChan < nChans; ++kChan)
            {
                int chan = data.channelInds[icaChansRel[kChan]];

                if (additive)
                {
                    buffer.addFrom(chan, 0, componentBuffer, comp, 0, nSamps, op.mixing(kChan, comp));
                }
                else
                {
                    buffer.addFrom(chan, 0, componentBuffer, comp, 0, nSamps, -op.mixing(kChan, comp));
                }
            }
        }
    }
}


void ICANode::updateSettings()
{
    jassert(!CoreServices::getAcquisitionStatus()); // just to be sure...

    int nChans = getNumInputs();

    // refresh subprocessor data
    uint32 newSubProc = 0;
    //subProcData.clear();
    subProcInfo.clear();

    std::map<uint32, SubProcData> newSubProcData;

    for (int c = 0; c < nChans; ++c)
    {
        const DataChannel* chan = getDataChannel(c);
        uint16 sourceID = chan->getSourceNodeID();
        uint16 subProcIdx = chan->getSubProcessorIdx();
        uint32 sourceFullId = getProcessorFullId(sourceID, subProcIdx);

        // assign this to be the next current subprocessor if
        // it is the same as the previous subproc; by default
        // use the first subprocessor encountered.
        if (sourceFullId == currSubProc || newSubProc == 0)
        {
            newSubProc = sourceFullId;
        }

        // see if it exists in the new map
        auto newDataEntry = newSubProcData.find(sourceFullId);
        if (newDataEntry != newSubProcData.end()) // found in new map
        {
            newDataEntry->second.channelInds.add(c);
            subProcInfo[sourceFullId].channelNames.add(chan->getName());
        }
        else // not found in new map
        {
            // make new entries

            SubProcInfo& newInfo = subProcInfo[sourceFullId];
            newInfo.sourceID = sourceID;
            newInfo.subProcIdx = subProcIdx;
            newInfo.sourceName = chan->getSourceName();
            newInfo.channelNames.add(chan->getName());

            SubProcData& newData = newSubProcData[sourceFullId];

            newData.Fs = chan->getSampleRate();
            newData.dsStride = jmax(int(newData.Fs / icaTargetFs), 1);
            newData.dsOffset = 0;
            newData.channelInds.add(c);
            newData.icaOp = new ICAOperation(); // null operation by default
            newData.icaConfigPath = "";

            // see whether there's a data entry in the old map to use
            auto oldDataEntry = subProcData.find(sourceFullId);
            if (oldDataEntry != subProcData.end()) // found in old map
            {
                // copy data from old map, to reuse cahce and 
                // potentially keep using existing icaOperation
                SubProcData& oldData = oldDataEntry->second;
                newData.dataCache = oldData.dataCache;

                // the ICA op really shouldn't be being used now, but just in case...
                const ScopedWriteLock icaWriteLock(oldData.icaMutex);
                newData.icaOp.swapWith(oldData.icaOp);
                newData.icaConfigPath.referTo(oldData.icaConfigPath);
            }
            else
            {
                newData.dataCache = new AudioBufferFifo(1, icaSamples);
            }
        }
    }

    // do things that require knowing # channels per subproc
    int maxSubProcChans = 0;

    for (auto& dataEntry : newSubProcData)
    {
        uint32 subProc = dataEntry.first;
        SubProcData& data = dataEntry.second;
        int nChans = data.channelInds.size();
        maxSubProcChans = jmax(maxSubProcChans, nChans);

        AudioBufferFifo::LockHandle dataHandle(*data.dataCache);
        dataHandle.resetWithSize(nChans, icaSamples);

        // if there is an existing icaOp, see whether it can be reused
        // (requires that the enabled channels are in the range of channels in this subproc)

        if (!data.icaOp->isNoop() && data.icaOp->enabledChannels.getLast() >= nChans)
        {
            // can't use, needs too many channels - reset to no-op
            data.icaOp = new ICAOperation();
            data.icaConfigPath = "";
        }
    }

    subProcData.swap(newSubProcData);
    
    currSubProc = newSubProc;

    if (currSubProc == 0)
    {
        currICAConfigPath = "";
        currPctFull = 0;
    }
    else
    {
        SubProcData& data = subProcData[currSubProc];
        currICAConfigPath.referTo(data.icaConfigPath);
        currPctFull.referTo(data.dataCache->getPctFull());
    }

    // ensure space for maximum # of components
    componentBuffer.setSize(maxSubProcChans, componentBuffer.getNumSamples());
}


void ICANode::saveCustomParametersToXml(XmlElement* parentElement)
{
    for (const auto& subProcEntry : subProcData)
    {
        uint32 subProc = subProcEntry.first;
        const SubProcData& data = subProcEntry.second;

        const ScopedReadLock icaLock(data.icaMutex);
        if (data.icaOp && !data.icaOp->isNoop())
        {
            XmlElement* opNode = parentElement->createNewChildElement("ICA_OP");

            opNode->setAttribute("configFile", data.icaConfigPath.toString());
            opNode->setAttribute("subproc", int(subProc));
            opNode->setAttribute("subprocChans", intSetToString(data.icaOp->enabledChannels));
            opNode->setAttribute("reject", intSetToString(data.icaOp->rejectedComponents));

            // add base64-encoded matrices
            XmlElement* mixingNode = opNode->createNewChildElement("MIXING");
            saveMatrixToXml(mixingNode, data.icaOp->mixing);

            XmlElement* unmixingNode = opNode->createNewChildElement("UNMIXING");
            saveMatrixToXml(unmixingNode, data.icaOp->unmixing);
        }
    }
}

void ICANode::loadCustomParametersFromXml()
{
    if (parametersAsXml != nullptr)
    {
        // each subproc should either load in an ICA operation or be reset.
        for (const auto& subProcEntry : subProcData)
        {
            uint32 subProc = subProcEntry.first;
            resetICA(subProc, true);

            forEachXmlChildElementWithTagName(*parametersAsXml, opNode, "ICA_OP")
            {

                if (!opNode->getIntAttribute("subproc") == subProc)
                {
                    continue;
                }

                String configFile = opNode->getStringAttribute("configFile");
                if (configFile.isEmpty())
                {
                    continue;
                }

                SortedSet<int> rejectSet = stringToIntSet(opNode->getStringAttribute("reject"));
                Result res = loadICA(configFile, subProc, &rejectSet);

                if (res.failed())
                {
                    // try loading matrices from XML file directly
                    XmlElement* mixingNode = opNode->getChildByName("MIXING");
                    XmlElement* unmixingNode = opNode->getChildByName("UNMIXING");

                    if (!mixingNode || !unmixingNode)
                    {
                        // give up
                        std::cerr << res.getErrorMessage() << std::endl;
                        continue;
                    }

                    ICARunInfo loadedInfo;
                    loadedInfo.subProc = subProc;
                    loadedInfo.config = configFile;
                    loadedInfo.op = new ICAOperation();
                    loadedInfo.op->rejectedComponents.swapWith(rejectSet);
                    loadedInfo.op->enabledChannels = stringToIntSet(opNode->getStringAttribute("subprocChans"));

                    int size = loadedInfo.op->enabledChannels.size();

                    loadedInfo.nChannels = size;
                    loadedInfo.op->mixing.resize(size, size);
                    loadedInfo.op->unmixing.resize(size, size);

                    res = readMatrixFromXml(mixingNode, loadedInfo.op->mixing);
                    if (res.wasOk())
                    {
                        res = readMatrixFromXml(unmixingNode, loadedInfo.op->unmixing);
                    }

                    if (res.wasOk())
                    {
                        res = setNewICAOp(loadedInfo);
                    }

                    if (res.failed())
                    {
                        std::cerr << "Failed to load ICA operation: " << res.getErrorMessage() << std::endl;
                    }
                }
            }
        }
    }
}


Component* ICANode::getCanvas() const
{
    return static_cast<VisualizerEditor*>(getEditor())->canvas;
}


float ICANode::getTrainDurationSec() const
{
    return icaSamples / icaTargetFs;
}

void ICANode::setTrainDurationSec(float dur)
{
    jassert(dur > 0);
    icaSamples = int(dur * icaTargetFs);

    // actually resize data caches
    for (auto& subProcEntry : subProcData)
    {
        SubProcData& data = subProcEntry.second;
        AudioBufferFifo::LockHandle hCache(*data.dataCache);
        hCache.resizeKeepingData(icaSamples);
    }
}


String ICANode::getDirSuffix() const
{
    return icaDirSuffix.trimCharactersAtStart("_");
}

void ICANode::setDirSuffix(const String& suffix)
{
    icaDirSuffix = suffix.isEmpty() ? String() : "_" + suffix;
}

const std::map<uint32, SubProcInfo>& ICANode::getSubProcInfo() const
{
    return subProcInfo;
}

uint32 ICANode::getCurrSubProc() const
{
    return currSubProc;
}

void ICANode::setCurrSubProc(uint32 fullId)
{
    if (fullId == 0)
    {
        currSubProc = fullId;
        currICAConfigPath = "";
        currPctFull = 0;
    }
    else
    {
        auto newSubProcData = subProcData.find(fullId);
        if (newSubProcData == subProcData.end())
        {
            jassertfalse;
            return;
        }
        currSubProc = fullId;
        currICAConfigPath.referTo(newSubProcData->second.icaConfigPath);
        currPctFull.referTo(newSubProcData->second.dataCache->getPctFull());
    }
}

const StringArray* ICANode::getCurrSubProcChannelNames() const
{
    auto currSubProcInfo = subProcInfo.find(currSubProc);
    if (currSubProcInfo == subProcInfo.end())
    {
        return nullptr;
    }

    return &currSubProcInfo->second.channelNames;
}


const Value& ICANode::addPctFullListener(Value::Listener* listener)
{
    if (listener)
    {
        currPctFull.addListener(listener);
    }
    return currPctFull;
}

const Value& ICANode::addConfigPathListener(Value::Listener* listener)
{
    if (listener)
    {
        currICAConfigPath.addListener(listener);
    }
    return currICAConfigPath;
}

const Value& ICANode::addICARunningListener(Value::Listener* listener)
{
    if (listener)
    {
        icaRunning.addListener(listener);
    }
    return icaRunning;
}


const ICAOperation* ICANode::readICAOperation(ScopedPointer<ScopedReadLock>& lock) const
{
    auto subProcEntry = subProcData.find(currSubProc);
    if (subProcEntry == subProcData.end())
    {
        return nullptr;
    }

    lock = new ScopedReadLock(subProcEntry->second.icaMutex);
    const ICAOperation* op = subProcEntry->second.icaOp;
    if (op->isNoop())
    {
        lock = nullptr;
        return nullptr;
    }

    return op;
}

ICAOperation* ICANode::writeICAOperation(ScopedPointer<ScopedWriteLock>& lock) const
{
    auto subProcEntry = subProcData.find(currSubProc);
    if (subProcEntry == subProcData.end())
    {
        return nullptr;
    }

    lock = new ScopedWriteLock(subProcEntry->second.icaMutex);
    ICAOperation* op = subProcEntry->second.icaOp;
    if (op->isNoop())
    {
        lock = nullptr;
        return nullptr;
    }

    return op;
}


File ICANode::getICABaseDir()
{
    if (CoreServices::getRecordingStatus())
    {
        return CoreServices::RecordNode::getRecordingPath();
    }

    return File::getSpecialLocation(File::hostApplicationPath)
        .getParentDirectory()
        .getChildFile("ica_runs");
}

// ICA thread

void ICANode::run()
{
    icaRunning = true;

    ICARunInfo info;

    // a little precarious since all the subfunctions have to have the exact same signature
    // (aside from their names)
    // could make it more flexible by using std::function, but that's probably worth avoiding.
    for (auto subfunc :
    {
        &ICANode::prepareICA,
        &ICANode::writeCacheData,
        &ICANode::performICA,
        &ICANode::processResults,
        &ICANode::setRejectedCompsBasedOnCurrent,
        &ICANode::setNewICAOp
    })
    {
        Result res = (this->*subfunc)(info);

        if (threadShouldExit())
        {
            break;
        }

        if (res.failed())
        {
            CoreServices::sendStatusMessage("ICA failed: " + res.getErrorMessage());
            break;
        }
    }

    icaRunning = false;    
}

Result ICANode::prepareICA(ICARunInfo& info)
{
    info.op = new ICAOperation();

    // collect current settings

    info.subProc = currSubProc;

    if (info.subProc == 0)
    {
        return Result::fail("No subprocessor selected");
    }

    auto subProcEntry = subProcData.find(info.subProc);
    if (subProcEntry == subProcData.end())
    {
        return Result::fail(String("Subprocessor ") + info.subProc + " no longer exists");
    }

    const SubProcData& currSubProcData = subProcEntry->second;

    // enabled channels = which channels of current subprocessor are enabled
    const SortedSet<int>& subProcChans = currSubProcData.channelInds;
    int nSubProcChans = subProcChans.size();

    GenericEditor* ed = getEditor();
    for (int c = 0; c < nSubProcChans; ++c)
    {
        bool p, r, a;
        int chan = subProcChans[c];
        ed->getChannelSelectionState(chan, &p, &r, &a);
        if (p)
        {
            info.op->enabledChannels.add(c);
        }
    }

    if (info.op->enabledChannels.size() < 2)
    {
        return Result::fail("At least 2 channels must be enabled to run ICA");
    }

    info.nChannels = info.op->enabledChannels.size();

    // find directory to save everything
    File baseDir = getICABaseDir();

    if (!baseDir.isDirectory())
    {
        Result res = baseDir.createDirectory();
        if (res.failed())
        {
            return Result::fail("Failed to make ICA runs directory ("
                + res.getErrorMessage().trimEnd() + ")");
        }
    }

    // create a subdirectory for files, which we want to make sure doesn't exist yet
    while (true)
    {
        if (currentThreadShouldExit()) { return Result::ok(); }

        String time = Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
        File outDir = baseDir.getChildFile("ICA_" + time + icaDirSuffix);
        if (!outDir.isDirectory())
        {
            Result res = outDir.createDirectory();
            if (res.failed())
            {
                return Result::fail("Failed to make output directory ("
                    + res.getErrorMessage().trimEnd() + ")");
            }

            info.config = outDir.getChildFile(configFilename);
            break;
        }
    }

    return Result::ok();
}

Result ICANode::writeCacheData(ICARunInfo& info)
{
    AudioBufferFifo& dataCache = *subProcData[info.subProc].dataCache;

    File icaDir = info.config.getParentDirectory();
    File inputFile = icaDir.getChildFile(inputFilename);

    while (true)
    {
        if (currentThreadShouldExit()) { return Result::ok(); }

        // shouldn't be contentious since the cache is supposedly full,
        // but avoid blocking with a try lock just in case
        AudioBufferFifo::TryLockHandle hData(dataCache);
        if (!hData.isLocked())
        {
            wait(100);
            continue;
        }

        // ok, we have the lock. so is the buffer really full or did the
        // length get increased at the last minute?
        if (!hData.isFull())
        {
            // (unlikely)
            return Result::fail("Data cache not full yet");
        }

        // alright, it's really full, we can write it out
        Result writeRes = hData.writeChannelsToFile(inputFile, info.op->enabledChannels);
        if (writeRes.wasOk())
        {
            info.nSamples = dataCache.getNumSamples();
            return Result::ok();
        }
        else
        {
            return Result::fail("Failed to write data to input file ("
                + writeRes.getErrorMessage().trimEnd() + ")");
        }
    }
}

Result ICANode::performICA(ICARunInfo& info)
{
    // Write config file. For now, not configurable, but maybe can be in the future.

    { // scope in which configStream exists
        FileOutputStream configStream(info.config);
        if (configStream.failedToOpen())
        {
            return Result::fail("Failed to open binica config file");
        }

        // skips some settings where the default is ok
        configStream << "# binica config file - for details, see https://sccn.ucsd.edu/wiki/Binica \n";

        // hint for loading - write which channels are enabled
        configStream << chanHintPrefix << intSetToString(info.op->enabledChannels) << '\n';

        configStream << "DataFile " << inputFilename << '\n';
        configStream << "chans " << info.nChannels << '\n';
        configStream << "frames " << info.nSamples << '\n';
        configStream << "WeightsOutFile " << weightFilename << '\n';
        configStream << "SphereFile " << sphereFilename << '\n';
        configStream << "maxsteps 512\n";
        configStream << "posact off\n";
        configStream << "annealstep 0.98\n";
        configStream.flush();

        Result status = configStream.getStatus();
        if (status.failed())
        {
            return Result::fail("Failed to write to config file ("
                + status.getErrorMessage().trimEnd() + ")");
        }
    }

    info.weight = info.config.getParentDirectory().getChildFile(weightFilename);
    info.sphere = info.config.getParentDirectory().getChildFile(sphereFilename);
    
    // do it!
    ICAProcess proc(info.config);

    while (proc.isRunning())
    {
        if (currentThreadShouldExit()) { return Result::ok(); }
        sleep(200);
    }

    if (proc.failedToRun())
    {
        return Result::fail("ICA failed to start");
    }

    int32 exitCode = proc.getExitCode();
    if (exitCode != 0)
    {
        return Result::fail("ICA failed with exit code " + String(exitCode));
    }

    return Result::ok();
}

Result ICANode::processResults(ICARunInfo& info)
{
    if (info.op->unmixing.size() == 0) // skip this if we already have an unmixing matrix
    {
        // load weight and sphere matrices into Eigen Map objects
        int size = info.nChannels;
        int sizeSq = size * size;

        Matrix weights(size, size);
        Result res = readMatrix(info.weight, weights);
        if (res.failed())
        {
            return res;
        }

        if (currentThreadShouldExit()) { return Result::ok(); }

        Matrix sphere(size, size);
        res = readMatrix(info.sphere, sphere);
        if (res.failed())
        {
            return res;
        }

        if (currentThreadShouldExit()) { return Result::ok(); }

        // now just need to convert this to mixing and unmixing

        // normalize sphere matrix by largest singular value
        Eigen::BDCSVD<Matrix> svd(sphere);
        Matrix normSphere = sphere / svd.singularValues()(0);

        info.op->unmixing = weights * normSphere;
    }

    info.op->mixing = info.op->unmixing.inverse();

    if (currentThreadShouldExit()) { return Result::ok(); }

    // write final matrices to output files
    File icaDir = info.config.getParentDirectory();

    Result res = saveMatrix(icaDir.getChildFile(unmixingFilename), info.op->unmixing);
    if (res.failed())
    {
        return res;
    }

    res = saveMatrix(icaDir.getChildFile(mixingFilename), info.op->mixing);
    if (res.failed())
    {
        return res;
    }

    return Result::ok();
}

Result ICANode::setRejectedCompsBasedOnCurrent(ICARunInfo& info)
{
    auto subProcEntry = subProcData.find(info.subProc);
    if (subProcEntry == subProcData.end())
    {
        return Result::fail(String("Subprocessor ") + info.subProc + " does not exist");
    }

    SubProcData& thisSubProcData = subProcEntry->second;

    while (true)
    {
        if (currentThreadShouldExit()) { return Result::ok(); }

        const ScopedReadTryLock icaLock(thisSubProcData.icaMutex);
        if (!icaLock.isLocked())
        {
            continue;
        }

        const ICAOperation& op = *thisSubProcData.icaOp;
        if (op.isNoop())
        {
            info.op->rejectedComponents.add(0);
        }
        else
        {
            info.op->rejectedComponents = op.rejectedComponents;
        }

        return Result::ok();
    }
}

Result ICANode::setNewICAOp(ICARunInfo& info)
{
    auto subProcEntry = subProcData.find(info.subProc);
    if (subProcEntry == subProcData.end())
    {
        return Result::fail(String("Subprocessor ") + info.subProc + " no longer exists");
    }

    SubProcData& currSubProcData = subProcEntry->second;

    if (info.op->enabledChannels.getLast() >= currSubProcData.channelInds.size())
    {
        return Result::fail("Operation needs more channels than are present in subprocessor " + info.subProc);
    }

    while (true)
    {
        if (currentThreadShouldExit()) { return Result::ok(); }

        const ScopedWriteTryLock icaLock(currSubProcData.icaMutex);
        if (!icaLock.isLocked())
        {
            continue;
        }

        ScopedPointer<ICAOperation>& oldOp = currSubProcData.icaOp;

        // see whether current rejected components are invalid
        if (info.op->rejectedComponents.getLast() >= info.op->enabledChannels.size())
        {
            std::cerr << "Warning: rejected component set in loaded ICA op names nonexistent components" << std::endl;
            std::cerr << "Defaulting to rejecting first component" << std::endl;

            info.op->rejectedComponents.clearQuick();
            info.op->rejectedComponents.add(0);
        }

        oldOp.swapWith(info.op);
        currSubProcData.icaConfigPath = info.config.getFullPathName();

        return Result::ok();
    }
}


Result ICANode::loadICA(const File& configFile, uint32 subProc, const SortedSet<int>* rejectSet)
{
    // populate a new ICARunInfo struct
    ICARunInfo loadedInfo;
    loadedInfo.op = new ICAOperation();
    loadedInfo.subProc = subProc;
    loadedInfo.config = configFile;

    Result res = populateInfoFromConfig(loadedInfo);

    if (res.wasOk())
    {
        if (rejectSet)
        {
            loadedInfo.op->rejectedComponents = *rejectSet;
        }
        else
        {
            res = setRejectedCompsBasedOnCurrent(loadedInfo);
        }
    }

    if (res.wasOk())
    {
        res = setNewICAOp(loadedInfo);
    }

    return res;
}


Result ICANode::populateInfoFromConfig(ICARunInfo& info)
{
    FileInputStream configStream(info.config);
    if (configStream.failedToOpen())
    {
        return Result::fail("Failed to open config file ("
            + configStream.getStatus().getErrorMessage().trimEnd() + ")");
    }

    StringArray configTokens;
    while (!configStream.isExhausted())
    {
        String line = configStream.readNextLine();

        Result res = configStream.getStatus();
        if (res.failed())
        {
            return Result::fail("Failed to read config file ("
                + res.getErrorMessage().trimEnd() + ")");
        }

        // handle enabled channels hint
        if (line.startsWith(chanHintPrefix))
        {
            SortedSet<int> enabledChans = stringToIntSet(line.fromFirstOccurrenceOf(chanHintPrefix, false, false));
            info.op->enabledChannels.swapWith(enabledChans);
        }
        else
        {
            StringArray tokens = StringArray::fromTokens(line.initialSectionNotContaining("!#%"), true);
            configTokens.addArray(tokens);
        }
    }

    int nTokens = configTokens.size();
    if (nTokens % 2 == 1)
    {
        return Result::fail("Malformed config file");
    }

    // fields of interest: chans, WeightsOutFile, SphereFile
    // all are required.
    File configDir = info.config.getParentDirectory();

    for (const String* tok = configTokens.begin(); tok < configTokens.end() - 1; tok += 2)
    {
        if (tok->equalsIgnoreCase("chans") || tok->equalsIgnoreCase("chan"))
        {
            info.nChannels = tok[1].getIntValue();
        }
        else if (tok->equalsIgnoreCase("weightsoutfile"))
        {
            info.weight = configDir.getChildFile(tok[1]);
        }
        else if (tok->equalsIgnoreCase("spherefile"))
        {
            info.sphere = configDir.getChildFile(tok[1]);
        }
    }

    if (info.nChannels < 2) { return Result::fail("Invalid or missing # of channels"); }

    if (info.op->enabledChannels.isEmpty())
    {
        CoreServices::sendStatusMessage("Warning: no enabled channels hint found, assuming "
            "first " + String(info.nChannels) + " channels");
        
        for (int i = 0; i < info.nChannels; ++i)
        {
            info.op->enabledChannels.add(i);
        }
    }
    else if (info.op->enabledChannels.size() != info.nChannels)
    {
        return Result::fail("Inconsistent number of channels");
    }

    // see whether we can use existing mixing and unmixing files
    File mixingFile   = configDir.getChildFile(mixingFilename);
    File unmixingFile = configDir.getChildFile(unmixingFilename);

    if (!info.weight.existsAsFile()) { return Result::fail("Invalid or missing weight file"); }
    if (!info.sphere.existsAsFile()) { return Result::fail("Invalid or missing sphere file"); }

    info.op->unmixing.resize(info.nChannels, info.nChannels);
    info.op->mixing.resize(info.nChannels, info.nChannels);

    Result res = readMatrix(unmixingFile, info.op->unmixing);
    if (res.wasOk())
    {
        res = readMatrix(mixingFile, info.op->mixing);
    }

    if (res.failed())
    {
        // try using weight and sphere files
        res = processResults(info);
    }

    return res;
}


Result ICANode::saveMatrix(const File& dest, MatrixConstRef mat)
{
    String fn = dest.getFileName();

    FileOutputStream stream(dest);

    if (stream.failedToOpen())
    {
        return Result::fail("Failed to open " + fn);
    }

    stream.write(mat.data(), mat.size() * sizeof(float));
    Result status = stream.getStatus();
    if (status.failed())
    {
        return Result::fail("Failed to write " + fn
            + "(" + status.getErrorMessage().trimEnd() + ")");
    }

    return Result::ok();
}


Result ICANode::readMatrix(const File& source, MatrixRef dest)
{
    String fn = source.getFileName();

    if (!source.existsAsFile())
    {
        return Result::fail("Matrix file " + fn + " not found");
    }

    FileInputStream stream(source);
    if (stream.failedToOpen())
    {
        return Result::fail("Failed to open " + fn
            + " (" + stream.getStatus().getErrorMessage().trimEnd() + ")");
    }

    int size = dest.rows();
    jassert(dest.cols() == size); // should always be a square matrix

    if (stream.getTotalLength() != size * size * sizeof(float))
    {
        return Result::fail(fn + " has incorrect length");
    }

    HeapBlock<float> destBlock(size * size);
    stream.read(destBlock, size * size * sizeof(float));

    Result status = stream.getStatus();
    if (status.failed())
    {
        return Result::fail("Failed to read " + fn
            + " (" + status.getErrorMessage().trimEnd() + ")");
    }

    dest = MatrixMap(destBlock, size, size);

    return Result::ok();
}


void ICANode::saveMatrixToXml(XmlElement* xml, MatrixConstRef mat)
{
    int size = mat.rows();
    jassert(mat.cols() == size);

    xml->setAttribute("size", size);
    
    String base64Mat = Base64::toBase64(mat.data(), sizeof(float) * size * size);
    xml->addTextElement(base64Mat);
}

Result ICANode::readMatrixFromXml(const XmlElement* xml, MatrixRef dest)
{
    int size = xml->getIntAttribute("size");
    if (size != dest.rows() || size != dest.cols())
    {
        return Result::fail("Matrix in XML does not match expected size");
    }

    HeapBlock<float> destBlock(size * size);
    MemoryOutputStream matStream(destBlock, sizeof(float) * size * size);

    String base64Mat = xml->getAllSubText();
    if (!Base64::convertFromBase64(matStream, base64Mat))
    {
        return Result::fail("Matrix in XML could not be converted from base64");
    }

    dest = MatrixMap(destBlock, size, size);

    return Result::ok();
}


String ICANode::intSetToString(const SortedSet<int>& set)
{
    String setString = "";
    bool first = true;
    for (int i : set)
    {
        if (!first)
        {
            setString += " ";
        }
        else
        {
            first = false;
        }
        setString += String(i);
    }
    return setString;
}

SortedSet<int> ICANode::stringToIntSet(const String& string)
{
    SortedSet<int> stringSet;

    StringArray intStrs = StringArray::fromTokens(string, false);
    intStrs.removeEmptyStrings();

    for (const String& intStr : intStrs)
    {
        stringSet.add(intStr.getIntValue());
    }

    return stringSet;
}


/**** SubProcInfo ****/

bool SubProcInfo::operator==(const SubProcInfo& other) const
{
    return sourceID == other.sourceID && subProcIdx == other.subProcIdx;
}

SubProcInfo::operator String() const
{
    return sourceName + " " + String(sourceID) + "/" + String(subProcIdx);
}


/****  AudioBufferFifo ****/

AudioBufferFifo::AudioBufferFifo(int numChans, int numSamps)
    : data(new AudioSampleBuffer(numChans, numSamps))
{
    reset();
}

int AudioBufferFifo::getNumSamples() const
{
    return data->getNumSamples();
}

const Value& AudioBufferFifo::getPctFull() const
{
    return pctFull;
}

void AudioBufferFifo::updateFullStatus()
{
    int numSamps = data->getNumSamples();
    pctFull = numSamps == 0 ? 0 : int(100 * (double(numWritten) / numSamps));
}

void AudioBufferFifo::reset()
{
    startPoint = 0;
    numWritten = 0;
    pctFull = 0;
}


/**  AudioBufferFifo handles **/

AudioBufferFifo::Handle::Handle(AudioBufferFifo& fifoIn)
    : fifo(fifoIn)
{}

bool AudioBufferFifo::Handle::isFull() const
{
    if (!isValid()) { return false; }

    return fifo.numWritten > 0 && fifo.numWritten == fifo.data->getNumSamples();
}

void AudioBufferFifo::Handle::reset()
{
    if (!isValid()) { return; }

    fifo.reset();
}

void AudioBufferFifo::Handle::resetWithSize(int numChans, int numSamps)
{
    if (!isValid()) { return; }

    jassert(numChans >= 0 && numSamps >= 0);
    fifo.data->setSize(numChans, numSamps);
    fifo.reset();
}


void AudioBufferFifo::Handle::copySample(const AudioSampleBuffer& source,
    const SortedSet<int>& channels, int sample)
{
    if (!isValid()) { return; }

    int numSamps = fifo.data->getNumSamples();
    if (numSamps < 1) { return; }

    int numChans = fifo.data->getNumChannels();
    jassert(channels.size() == numChans);

    int destSample = (fifo.startPoint + fifo.numWritten) % numSamps;

    for (int c = 0; c < numChans; ++c)
    {
        int sourceChan = channels[c];
        jassert(sourceChan >= 0 && sourceChan < source.getNumChannels());
        fifo.data->setSample(c, destSample, source.getSample(sourceChan, sample));
    }

    if (fifo.numWritten < numSamps)
    {
        fifo.numWritten++;
    }
    else
    {
        jassert(destSample == fifo.startPoint);
        fifo.startPoint = (fifo.startPoint + 1) % numSamps;
    }

    fifo.updateFullStatus();
}


void AudioBufferFifo::Handle::resizeKeepingData(int numSamps)
{
    if (!isValid()) { return; }

    int numChans = fifo.data->getNumChannels();
    int currNumSamps = fifo.data->getNumSamples();

    if (currNumSamps == numSamps) { return; }

    if (fifo.startPoint + fifo.numWritten <= numSamps)
    {
        // all in one block that will fit in new size - no special handling required
        fifo.data->setSize(numChans, numSamps, true);
    }
    else
    {
        ScopedPointer<AudioSampleBuffer> tempData(new AudioSampleBuffer(numChans, numSamps));

        int newNumWritten = jmin(fifo.numWritten, numSamps);

        // could use an AbstractFifo maybe but w/e
        int block1Start = fifo.startPoint;
        int block1Size = jmin(newNumWritten, currNumSamps - block1Start);

        int block2Start = 0;
        int block2Size = newNumWritten - block1Size;

        for (int c = 0; c < numChans; ++c)
        {
            tempData->copyFrom(c, 0, *fifo.data, c, block1Start, block1Size);
            tempData->copyFrom(c, block1Size, *fifo.data, c, block2Start, block2Size);
        }

        fifo.data.swapWith(tempData);
        fifo.startPoint = 0;
        fifo.numWritten = newNumWritten;
    }

    fifo.updateFullStatus();
}


Result AudioBufferFifo::Handle::writeChannelsToFile(const File& file, const SortedSet<int>& channels)
{
    if (!isValid())
    {
        jassertfalse;
        return Result::fail("Invalid handle to data cache");
    }

    FileOutputStream stream(file);
    if (!stream.openedOk())
    {
        return stream.getStatus();
    }

    jassert(isFull()); // should only be called when the FIFO is full...

    int numChans = fifo.data->getNumChannels();
    int numSamps = fifo.data->getNumSamples();

    for (int chan : channels)
    {
        jassert(chan >= 0 && chan < numChans);
    }

    for (int s = 0; s < numSamps; ++s)
    {
        int samp = (fifo.startPoint + s) % numSamps;

        for (int chan : channels)
        {
            if (!stream.writeFloat(fifo.data->getSample(chan, samp)))
            {
                return stream.getStatus();
            }
        }
    }

    stream.flush();
    return stream.getStatus();
}

bool AudioBufferFifo::Handle::isValid() const
{
    return true;
}


AudioBufferFifo::LockHandle::LockHandle(AudioBufferFifo& fifoIn)
    : Handle        (fifoIn)
    , ScopedLock    (fifoIn.mutex)
{}

AudioBufferFifo::TryLockHandle::TryLockHandle(AudioBufferFifo& fifoIn)
    : Handle        (fifoIn)
    , ScopedTryLock (fifoIn.mutex)
{}

bool AudioBufferFifo::TryLockHandle::isValid() const
{
    return isLocked();
}


/**** ScopedTryLocks for ReadWriteLocks ****/

ScopedReadTryLock::ScopedReadTryLock(const ReadWriteLock& lock) noexcept
    : lock_             (lock)
    , lockWasSuccessful (lock.tryEnterRead())
{}

ScopedReadTryLock::~ScopedReadTryLock() noexcept
{
    if (lockWasSuccessful)
    {
        lock_.exitRead();
    }
}

bool ScopedReadTryLock::isLocked() const noexcept
{
    return lockWasSuccessful;
}


ScopedWriteTryLock::ScopedWriteTryLock(const ReadWriteLock& lock) noexcept
    : lock_             (lock)
    , lockWasSuccessful (lock.tryEnterWrite())
{}

ScopedWriteTryLock::~ScopedWriteTryLock() noexcept
{
    if (lockWasSuccessful)
    {
        lock_.exitWrite();
    }
}

bool ScopedWriteTryLock::isLocked() const noexcept
{
    return lockWasSuccessful;
}