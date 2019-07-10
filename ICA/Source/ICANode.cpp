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

const String ICANode::inputFilename("input.floatdata");
const String ICANode::configFilename("binica.sc");
const String ICANode::weightFilename("output.wts");
const String ICANode::sphereFilename("output.sph");
const String ICANode::mixingFilename("mixing.dat");
const String ICANode::unmixingFilename("unmixing.dat");

ICANode::ICANode()
    : GenericProcessor          ("ICA")
    , ThreadWithProgressWindow  ("ICA Computation", true, true)
    , icaSamples                (int(icaTargetFs * 120))
    , componentBuffer           (1024)
    , currSubProc               (0)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
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

    launchThread();
    return true;
}

void ICANode::resetICA(uint32 subproc)
{
    auto dataEntry = subProcData.find(subproc);
    if (dataEntry != subProcData.end())
    {
        SubProcData& data = dataEntry->second;
        const ScopedWriteTryLock icaLock(data.icaMutex);
        if (!icaLock.isLocked())
        {
            // uh-oh, if it's being written to now, we don't want to
            // throw the new thing away
            // bail out
            return;
        }

        data.icaOp = new ICAOperation();
        data.icaConfigPath = "";
    }
}

void ICANode::loadICA(const File& configFile)
{
    // populate a new ICARunInfo struct
    ICARunInfo loadedInfo;
    loadedInfo.op = new ICAOperation();
    loadedInfo.config = configFile;
    loadedInfo.subProc = currSubProc;

    Result res = populateInfoFromConfig(loadedInfo);
    if (res.failed())
    {
        CoreServices::sendStatusMessage("ICA load failed: " + res.getErrorMessage());
        return;
    }

    while (!tryToSetNewICAOp(loadedInfo))
    {
        Thread::sleep(100);
    }
}

bool ICANode::disable()
{
    if (isThreadRunning())
    {
        stopThread(500);
    }

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
    componentBuffer.realloc(buffer.getNumSamples());

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
        
        const Array<int>& icaChansRel = op.enabledChannels;
        int nChans = icaChansRel.size();
        int nComps = op.components.size();

        for (int kComp = 0; kComp < nComps; ++kComp)
        {
            int comp = op.components[kComp];

            componentBuffer.clear(nSamps);

            // unmix into the components
            for (int kChan = 0; kChan < nChans; ++kChan)
            {
                int chan = data.channelInds[icaChansRel[kChan]]; 

                FloatVectorOperations::addWithMultiply(componentBuffer,
                    buffer.getReadPointer(chan), op.unmixing(kComp, kChan), nSamps);
            }

            // remix back into channels
            for (int kChan = 0; kChan < nChans; ++kChan)
            {
                int chan = data.channelInds[icaChansRel[kChan]];

                if (op.keep)
                {
                    // if we're keeping selected components, have to clear first
                    if (kComp == 0)
                    {
                        buffer.clear(chan, 0, nSamps);
                    }
                    buffer.addFrom(chan, 0, componentBuffer, nSamps, op.mixing(kChan, kComp));
                }
                else
                {
                    buffer.addFrom(chan, 0, componentBuffer, nSamps, -op.mixing(kChan, kComp));
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
    for (auto& dataEntry : newSubProcData)
    {
        uint32 subProc = dataEntry.first;
        SubProcData& data = dataEntry.second;
        int nChans = data.channelInds.size();

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
    SubProcData& data = subProcData[currSubProc];
    currICAConfigPath.referTo(data.icaConfigPath);
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
        currICAConfigPath.referTo(emptyVal);
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
    }
}


const Value& ICANode::getPctFullValue() const
{
    auto subProcEntry = subProcData.find(currSubProc);
    if (subProcEntry == subProcData.end())
    {
        return noInputVal;
    }

    return subProcEntry->second.dataCache->getPctFull();
}


const Value& ICANode::addConfigPathListener(Value::Listener* listener)
{
    if (listener)
    {
        currICAConfigPath.addListener(listener);
    }
    return currICAConfigPath;
}


const ICAOperation* ICANode::getICAOperation(ScopedPointer<ScopedReadLock>& lock) const
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


File ICANode::getICABaseDir()
{
    if (CoreServices::getRecordingStatus())
    {
        return CoreServices::RecordNode::getRecordingPath();
    }

    return File::getSpecialLocation(File::hostApplicationPath).getParentDirectory();
}

// ICA thread

void ICANode::run()
{
    setStatusMessage("Preparing...");

    ICARunInfo info;

    info.op = new ICAOperation();

    // collect current settings

    info.subProc = currSubProc;

    if (info.subProc == 0)
    {
        reportError("No subprocessor selected");
        return;
    }

    // enabled channels = which channels of current subprocessor are enabled
    const Array<int>& subProcChans = subProcData[info.subProc].channelInds;
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
        reportError("At least 2 channels must be enabled to run ICA");
        return;
    }

    info.nChannels = info.op->enabledChannels.size();

    // find directory to save everything
    File baseDir = getICABaseDir();

    // create a subdirectory for files, which we want to make sure doesn't exist yet
    while (true)
    {
        if (threadShouldExit()) { return; }

        String time = Time::getCurrentTime().formatted("%Y-%m-%d_%H-%M-%S");
        File outDir = baseDir.getChildFile("ICA_" + time + icaDirSuffix);
        if (!outDir.isDirectory())
        {
            Result res = outDir.createDirectory();
            if (res.failed())
            {
                reportError("Failed to make output directory ("
                    + res.getErrorMessage().trimEnd() + ")");
                return;
            }

            info.config = outDir.getChildFile(configFilename);
            break;
        }
    }

    using ICASubFunc = Result(ICANode::*)(ICARunInfo&);
    for (ICASubFunc sub : { &ICANode::prepareICA, &ICANode::performICA, &ICANode::processResults })
    {
        Result res = (this->*sub)(info);
        if (threadShouldExit())
        {
            return;
        }
        else if (res.failed())
        {
            reportError(res.getErrorMessage());
            return;
        }
    }

    while (!threadShouldExit() && !tryToSetNewICAOp(info))
    {
        wait(100);
    }
}

Result ICANode::prepareICA(ICARunInfo& info)
{
    setStatusMessage("Collecting data for ICA...");
    setProgress(0.0);

    AudioBufferFifo& dataCache = *subProcData[info.subProc].dataCache;

    File icaDir = info.config.getParentDirectory();
    File inputFile = icaDir.getChildFile(inputFilename);

    int nWritten = 0;
    bool written = false;
    while (!written)
    {
        // wait for cache to fill up
        while (!dataCache.isFull())
        {
            if (threadShouldExit()) { return Result::ok(); }

            setProgress(double(dataCache.getPctFull().getValue()) / 100.0);

            wait(100);
        }

        while (!written)
        {
            if (threadShouldExit()) { return Result::ok(); }

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
            if (!dataCache.isFull())
            {
                // start over on waiting for it to fill up
                break;
            }

            // alright, it's really full, we can write it out
            Result writeRes = hData.writeChannelsToFile(inputFile, info.op->enabledChannels);
            if (writeRes.wasOk())
            {
                written = true;
                info.nSamples = dataCache.getNumSamples();
            }
            else
            {
                return Result::fail("Failed to write data to input file ("
                    + writeRes.getErrorMessage().trimEnd() + ")");
            }
        }
    }

    setProgress(1.0);
    return Result::ok();
}

Result ICANode::performICA(ICARunInfo& info)
{
    setStatusMessage("Running ICA...");

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
        configStream << "!chans:";
        for (int chan : info.op->enabledChannels)
        {
            configStream << " " << chan;
        }
        configStream << '\n';

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
        if (threadShouldExit()) { return Result::ok(); }
        wait(200);
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
    const bool inThread = (Thread::getCurrentThreadId() == getThreadId());
    if (inThread)
    {
        setStatusMessage("Processing ICA results...");
    }

    if (info.op->unmixing.size() == 0) // skip this if we already have an unmixing matrix
    {
        // load weight and sphere matrices into Eigen Map objects
        int size = info.nChannels;
        int sizeSq = size * size;

        HeapBlock<float> weightBlock(sizeSq);

        Result res = readMatrix(info.weight, weightBlock, sizeSq);
        if (res.failed())
        {
            return res;
        }

        if (inThread && threadShouldExit()) { return Result::ok(); }

        HeapBlock<float> sphereBlock(sizeSq);

        res = readMatrix(info.sphere, sphereBlock, sizeSq);
        if (res.failed())
        {
            return res;
        }

        if (inThread && threadShouldExit()) { return Result::ok(); }

        MatrixMap weights(weightBlock.getData(), size, size);
        MatrixMap sphere(sphereBlock.getData(), size, size);

        // now just need to convert this to mixing and unmixing

        // normalize sphere matrix by largest singular value
        Eigen::BDCSVD<Matrix> svd(sphere);
        Matrix normSphere = sphere / svd.singularValues()(0);

        info.op->unmixing = weights * normSphere;
    }

    info.op->mixing = info.op->unmixing.inverse();

    if (inThread && threadShouldExit()) { return Result::ok(); }

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


bool ICANode::tryToSetNewICAOp(ICARunInfo& info)
{
    SubProcData& currSubProcData = subProcData[info.subProc];
    ScopedWriteTryLock icaLock(currSubProcData.icaMutex);

    if (!icaLock.isLocked())
    {
        return false;
    }

    // reject first component by default
    info.op->keep = false;
    info.op->components.add(0);

    // see if we can reuse an existing "components" array
    // this is only allowed if this subprocessor has an existing ICA transformation
    // and it is based on the same channels.
    ScopedPointer<ICAOperation>& oldOp = currSubProcData.icaOp;
    if (oldOp->enabledChannels == info.op->enabledChannels)
    {
        // take old op's components and keep
        info.op->components.swapWith(oldOp->components);
        info.op->keep = oldOp->keep;
    }

    oldOp.swapWith(info.op);
    currSubProcData.icaConfigPath = info.config.getFullPathName();

    return true;
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
        if (line.startsWith("!chans:"))
        {
            StringArray chanStrs = StringArray::fromTokens(line, false);
            chanStrs.remove(0);
            for (const String& chan : chanStrs)
            {
                info.op->enabledChannels.add(chan.getIntValue());
            }
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

    if (subProcData[currSubProc].channelInds.size() <= info.op->enabledChannels.getLast())
    {
        return Result::fail("Not enough channels in current subproc for selected ICA operation");
    }

    // see whether we can use existing mixing and unmixing files
    File mixingFile   = configDir.getChildFile(mixingFilename);
    File unmixingFile = configDir.getChildFile(unmixingFilename);

    int numel = info.nChannels * info.nChannels;
    HeapBlock<float> matStore(numel);
    if (readMatrix(unmixingFile, matStore, numel).wasOk())
    {
        info.op->unmixing = MatrixMap(matStore, info.nChannels, info.nChannels);

        if (readMatrix(mixingFile, matStore, numel).wasOk())
        {
            info.op->mixing = MatrixMap(matStore, info.nChannels, info.nChannels);
        }
    }
    else
    {
        if (!info.weight.existsAsFile()) { return Result::fail("Invalid or missing weight file"); }
        if (!info.sphere.existsAsFile()) { return Result::fail("Invalid or missing sphere file"); }
    }

    if (info.op->mixing.size() == 0)
    {
        Result res = processResults(info);
        if (res.failed())
        {
            return res;
        }
    }

    jassert(info.op->mixing.size() == numel);
    return Result::ok();
}

Result ICANode::readMatrix(const File& source, HeapBlock<float>& dest, int numel)
{
    String fn = source.getFileName();

    if (!source.existsAsFile())
    {
        return Result::fail("ICA did not create " + fn);
    }

    FileInputStream stream(source);
    if (stream.failedToOpen())
    {
        return Result::fail("Failed to open " + fn
            + " (" + stream.getStatus().getErrorMessage().trimEnd() + ")");
    }

    if (stream.getTotalLength() != numel * sizeof(float))
    {
        return Result::fail(fn + " has incorrect length");
    }

    stream.read(dest, numel * sizeof(float));
    Result status = stream.getStatus();
    if (status.failed())
    {
        return Result::fail("Failed to read " + fn
            + " (" + status.getErrorMessage().trimEnd() + ")");
    }

    return Result::ok();
}

Result ICANode::saveMatrix(const File& dest, const MatrixRef& mat)
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

void ICANode::reportError(const String& whatHappened)
{
    setStatusMessage("Error: " + whatHappened);
    CoreServices::sendStatusMessage("ICA failed: " + whatHappened);
    wait(5000);
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
    , full(false)
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

bool AudioBufferFifo::isFull() const
{
    return full.load(std::memory_order_acquire);
}

void AudioBufferFifo::updateFullStatus()
{
    int numSamps = data->getNumSamples();
    full.store(numWritten == numSamps, std::memory_order_release);
    int pctFullInt = numSamps == 0 ? 100 : int(100.0 * numWritten / numSamps + 0.5);
    pctFull = String(pctFullInt) + "% full)";
}

void AudioBufferFifo::reset()
{
    startPoint = 0;
    numWritten = 0;
    pctFull = "0% full)";
    full = false;
}


/**  AudioBufferFifo handles **/

AudioBufferFifo::Handle::Handle(AudioBufferFifo& fifoIn)
    : fifo(fifoIn)
{}

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
    const Array<int>& channels, int sample)
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


Result AudioBufferFifo::Handle::writeChannelsToFile(const File& file, const Array<int>& channels)
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

    jassert(fifo.full.load(std::memory_order_acquire)); // should only be called when the FIFO is full...

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
    : Handle({ fifoIn })
    , ScopedLock(fifoIn.mutex)
{}

AudioBufferFifo::TryLockHandle::TryLockHandle(AudioBufferFifo& fifoIn)
    : Handle({ fifoIn })
    , ScopedTryLock(fifoIn.mutex)
{}

bool AudioBufferFifo::TryLockHandle::isValid() const
{
    return isLocked();
}


/**** RWLock adapters ****/

RWLockReadAdapter::RWLockReadAdapter(const ReadWriteLock& lock)
    : lock(lock)
{}

void RWLockReadAdapter::enter() const
{
    lock.enterRead();
}

bool RWLockReadAdapter::tryEnter() const
{
    return lock.tryEnterRead();
}

void RWLockReadAdapter::exit() const
{
    lock.exitRead();
}

RWLockWriteAdapter::RWLockWriteAdapter(const ReadWriteLock& lock)
    : lock(lock)
{}

void RWLockWriteAdapter::enter() const
{
    lock.enterWrite();
}

bool RWLockWriteAdapter::tryEnter() const
{
    return lock.tryEnterWrite();
}

void RWLockWriteAdapter::exit() const
{
    lock.exitWrite();
}