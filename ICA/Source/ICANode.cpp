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


/****  AudioBufferFifo ****/

AudioBufferFifo::AudioBufferFifo(int numChans, int numSamps)
    : data          (new AudioSampleBuffer(numChans, numSamps))
    , full          (false)
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
    : Handle        ({ fifoIn })
    , ScopedLock    (fifoIn.mutex)
{}

AudioBufferFifo::TryLockHandle::TryLockHandle(AudioBufferFifo& fifoIn)
    : Handle        ({ fifoIn })
    , ScopedTryLock (fifoIn.mutex)
{}

bool AudioBufferFifo::TryLockHandle::isValid() const
{
    return isLocked();
}


/*****  ICANode *****/

// static members
const float ICANode::icaTargetFs    (500.0f);
const Value ICANode::noInputVal     (var("no input)"));
const Value ICANode::emptyVal       (var(""));

const String ICANode::inputFilename("input.floatdata");
const String ICANode::configFilename("binica.sc");
const String ICANode::weightFilename("output.wts");
const String ICANode::sphereFilename("output.sph");
const String ICANode::mixingFilename("mixing.dat");
const String ICANode::unmixingFilename("unmixing.dat");

ICANode::ICANode()
    : GenericProcessor          ("ICA")
    , ThreadWithProgressWindow  ("ICA Computation", true, true)
    , icaSamples                (int(icaTargetFs * 30))
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
        data.icaDir.setValue("");
        data.icaParentDir.clear();
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
        }
        else // not found in new map
        {
            // make new entries

            SubProcInfo& newInfo = subProcInfo[sourceFullId];
            newInfo.sourceID = sourceID;
            newInfo.subProcIdx = subProcIdx;
            newInfo.sourceName = chan->getSourceName();

            SubProcData& newData = newSubProcData[sourceFullId];

            newData.Fs = chan->getSampleRate();
            newData.dsStride = jmax(int(newData.Fs / icaTargetFs), 1);
            newData.dsOffset = 0;
            newData.channelInds.add(c);
            newData.icaOp = new ICAOperation(); // null operation by default
            newData.icaDir = String();

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
                newData.icaDir.referTo(oldData.icaDir);
                newData.icaParentDir.swapWith(oldData.icaParentDir);
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
            data.icaDir = String();
            data.icaParentDir.clear();
        }
    }

    subProcData.swap(newSubProcData);
    currSubProc = newSubProc;
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


bool ICANode::SubProcInfo::operator==(const SubProcInfo& other) const
{
    return sourceID == other.sourceID && subProcIdx == other.subProcIdx;
}

ICANode::SubProcInfo::operator String() const
{
    return sourceName + " " + String(sourceID) + "/" + String(subProcIdx);
}

const std::map<uint32, ICANode::SubProcInfo>& ICANode::getSubProcInfo() const
{
    return subProcInfo;
}

uint32 ICANode::getCurrSubProc() const
{
    return currSubProc;
}

void ICANode::setCurrSubProc(uint32 fullId)
{
    currSubProc = fullId;
}


const Value& ICANode::getPctFullValue() const
{
    if (currSubProc == 0)
    {
        return noInputVal;
    }

    return subProcData.at(currSubProc).dataCache->getPctFull();
}


const Value& ICANode::getICAOutputDirValue() const
{
    if (currSubProc == 0)
    {
        return emptyVal;
    }

    return subProcData.at(currSubProc).icaDir;
}

// ICA thread

void ICANode::run()
{
    setStatusMessage("Preparing...");

    icaRunInfo.op = new ICAOperation();

    // collect current settings

    icaRunInfo.subProc = currSubProc;

    if (icaRunInfo.subProc == 0)
    {
        reportError("No subprocessor selected");
        return;
    }

    // enabled channels = which channels of current subprocessor are enabled
    const Array<int>& subProcChans = subProcData[icaRunInfo.subProc].channelInds;
    int nSubProcChans = subProcChans.size();

    GenericEditor* ed = getEditor();
    for (int c = 0; c < nSubProcChans; ++c)
    {
        bool p, r, a;
        int chan = subProcChans[c];
        ed->getChannelSelectionState(chan, &p, &r, &a);
        if (p)
        {
            icaRunInfo.op->enabledChannels.add(c);
        }
    }

    if (icaRunInfo.op->enabledChannels.isEmpty())
    {
        reportError("No channels are enabled for the current subprocessor");
        return;
    }

    // find directory to save everything
    File baseDir;
    if (CoreServices::getRecordingStatus())
    {
        baseDir = CoreServices::RecordNode::getRecordingPath();
    }
    else
    {
        // default to "bin" dir
        baseDir = File::getSpecialLocation(File::hostApplicationPath).getParentDirectory();
    }

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

            icaRunInfo.dir = outDir;
            break;
        }
    }

    int nSamples = prepareICA();
    if (nSamples == 0 || threadShouldExit()) { return; }
    if (!performICA(nSamples) || threadShouldExit()) { return; }
    processResults();
}

int ICANode::prepareICA()
{
    setStatusMessage("Collecting data for ICA...");
    setProgress(0.0);

    AudioBufferFifo& dataCache = *subProcData[icaRunInfo.subProc].dataCache;

    File inputFile = icaRunInfo.dir.getChildFile(inputFilename);

    int nWritten = 0;
    bool written = false;
    while (!written)
    {
        // wait for cache to fill up
        while (!dataCache.isFull())
        {
            if (threadShouldExit()) { return 0; }

            setProgress(double(dataCache.getPctFull().getValue()) / 100.0);

            wait(100);
        }

        while (!written)
        {
            if (threadShouldExit()) { return 0; }

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
            Result writeRes = hData.writeChannelsToFile(inputFile, icaRunInfo.op->enabledChannels);
            if (writeRes.wasOk())
            {
                written = true;
                nWritten = dataCache.getNumSamples();
            }
            else
            {
                reportError("Failed to write data to input file ("
                    + writeRes.getErrorMessage().trimEnd() + ")");
                return 0;
            }
        }
    }

    setProgress(1.0);
    return nWritten;
}

bool ICANode::performICA(int nSamples)
{
    setStatusMessage("Running ICA...");
    setProgress(0.0);

    // Write config file. For now, not configurable, but maybe can be in the future.
    File configFile = icaRunInfo.dir.getChildFile(configFilename);

    { // scope in which configStream exists
        FileOutputStream configStream(configFile);
        if (configStream.failedToOpen())
        {
            reportError("Failed to open binica config file");
            return false;
        }

        // skips some settings where the default is ok
        configStream << "# binica config file - for details, see https://sccn.ucsd.edu/wiki/Binica \n";
        configStream << "DataFile " << inputFilename << '\n';
        configStream << "chans " << icaRunInfo.op->enabledChannels.size() << '\n';
        configStream << "frames " << nSamples << '\n';
        configStream << "WeightsOutFile " << weightFilename << '\n';
        configStream << "SphereFile " << sphereFilename << '\n';
        configStream << "maxsteps 512\n";
        configStream << "posact off\n";
        configStream << "annealstep 0.98\n";
        configStream.flush();

        Result status = configStream.getStatus();
        if (status.failed())
        {
            reportError("Failed to write to config file ("
                + status.getErrorMessage().trimEnd() + ")");
            return false;
        }
    }
    
    // do it!
    double progress = 0.05;
    setProgress(progress);

    ICAProcess proc(configFile);

    int samplesGuess = icaSamples;
    while (proc.isRunning())
    {
        if (threadShouldExit()) { return false; }
        wait(200);

        // stupid guess
        progress += (icaTargetFs * 60 / samplesGuess * 0.01 * (1 - progress));
        setProgress(progress);
    }

    if (proc.failedToRun())
    {
        reportError("ICA failed to start");
        return false;
    }

    int32 exitCode = proc.getExitCode();
    if (exitCode != 0)
    {
        reportError("ICA failed with exit code " + String(exitCode));
        return false;
    }

    setProgress(1.0);
    return true;
}

bool ICANode::processResults()
{
    setStatusMessage("Processing ICA results...");
    setProgress(0.0);

    // load weight and sphere matrices into Eigen Map objects
    int size = icaRunInfo.op->enabledChannels.size();
    int sizeSq = size * size;

    HeapBlock<float> weightBlock(sizeSq);
    File weightFile = icaRunInfo.dir.getChildFile(weightFilename);

    Result res = readOutput(weightFile, weightBlock, sizeSq);
    if (res.failed())
    {
        reportError(res.getErrorMessage());
        return false;
    }

    if (threadShouldExit()) { return false; }
    setProgress(0.2);

    HeapBlock<float> sphereBlock(sizeSq);
    File sphereFile = icaRunInfo.dir.getChildFile(sphereFilename);

    res = readOutput(sphereFile, sphereBlock, sizeSq);
    if (res.failed())
    {
        reportError(res.getErrorMessage());
        return false;
    }

    if (threadShouldExit()) { return false; }
    setProgress(0.4);

    Eigen::Map<Eigen::MatrixXf> weights(weightBlock.getData(), size, size);
    Eigen::Map<Eigen::MatrixXf> sphere(sphereBlock.getData(), size, size);

    // now just need to convert this to mixing and unmixing

    // normalize sphere matrix by largest singular value
    Eigen::BDCSVD<Eigen::MatrixXf> svd(sphere);
    Eigen::MatrixXf normSphere = sphere / svd.singularValues()(0);

    icaRunInfo.op->unmixing = weights * normSphere;
    icaRunInfo.op->mixing = icaRunInfo.op->unmixing.inverse();

    if (threadShouldExit()) { return false; }
    setProgress(0.6);

    // write final matrices to output files
    res = saveMatrix(icaRunInfo.dir.getChildFile(unmixingFilename), icaRunInfo.op->unmixing);
    if (res.failed())
    {
        reportError(res.getErrorMessage());
        return false;
    }

    res = saveMatrix(icaRunInfo.dir.getChildFile(mixingFilename), icaRunInfo.op->mixing);
    if (res.failed())
    {
        reportError(res.getErrorMessage());
        return false;
    }

    SubProcData& currSubProcData = subProcData[icaRunInfo.subProc];

    while (true)
    {
        if (threadShouldExit()) { return false; }

        ScopedWriteTryLock icaLock(currSubProcData.icaMutex);

        if (!icaLock.isLocked())
        {
            wait(100);
            continue;
        }

        setProgress(0.8);

        // reject first component by default
        icaRunInfo.op->keep = false;
        icaRunInfo.op->components.add(0);

        // see if we can reuse an existing "components" array
        // this is only allowed if this subprocessor has an existing ICA transformation
        // and it is based on the same channels.
        ScopedPointer<ICAOperation>& oldOp = currSubProcData.icaOp;
        if (oldOp->enabledChannels == icaRunInfo.op->enabledChannels)
        {
            // take old op's components and keep
            icaRunInfo.op->components.swapWith(oldOp->components);
            icaRunInfo.op->keep = oldOp->keep;
        }

        oldOp.swapWith(icaRunInfo.op);
        currSubProcData.icaDir.setValue(icaRunInfo.dir.getFileName());
        currSubProcData.icaParentDir = icaRunInfo.dir.getParentDirectory().getFullPathName();

        setProgress(1.0);
        return true;
    }
}

Result ICANode::readOutput(const File& source, HeapBlock<float>& dest, int numel)
{
    String fn = source.getFileName();

    if (!source.existsAsFile())
    {
        return Result::fail("ICA did not create " + fn);
    }

    FileInputStream stream(source);
    if (stream.failedToOpen())
    {
        return Result::fail("Failed to open " + fn);
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

Result ICANode::saveMatrix(const File& dest, const Eigen::Ref<const Eigen::MatrixXf>& mat)
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