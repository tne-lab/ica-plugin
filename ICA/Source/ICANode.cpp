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
    pctFull = String(int(100.0 * numWritten / numSamps + 0.5)) + "%";
}

void AudioBufferFifo::reset()
{
    startPoint = 0;
    numWritten = 0;
    pctFull = "0%";
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
const float ICANode::icaTargetFs        (500.0f);
const Value ICANode::nothingToCollect   (var("<no input>"));

ICANode::ICANode()
    : GenericProcessor  ("ICA")
    , icaSamples        (icaTargetFs * 30)
    , componentBuffer   (16, 1024)
    , currSubProc       (0)
    , icaDirSuffix      (String())
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
    if (icaRunner == nullptr || icaRunner->isThreadRunning())
    {
        return false;
    }

    icaRunner->launchThread();
    return true;
}

bool ICANode::disable()
{
    if (icaRunner != nullptr && icaRunner->isThreadRunning())
    {
        icaRunner->stopThread(500);
    }

    return true;
}

void ICANode::process(AudioSampleBuffer& buffer)
{
    // add data to cache for each subprocessor, if possible
    for (auto& subProcEntry : subProcData)
    {
        SubProcData& data = subProcEntry.second;
        AudioBufferFifo::TryLockHandle hCache(*data.dataCache);

        if (hCache.isLocked())
        {
            jassert(data.channelInds.size() > 0);
            int nSamps = getNumSamples(data.channelInds[0]);

            for (int s = data.dsOffset; s < nSamps; s += data.dsStride)
            {
                hCache.copySample(buffer, data.channelInds, s);
            }

            data.dsOffset -= nSamps;
        }
    }

    // do ICA!

    // should only happen on the first buffer, since they should all
    // be the same length during acquisition
    componentBuffer.setSize(componentBuffer.getNumChannels(), buffer.getNumSamples());

    RWSync::ReadPtr<ICAOutput> icaReader(icaCoefs);
    jassert(icaReader.isValid());
    if (!icaReader.canRead())
    {
        return; // probably no transformation has been computed yet
    }

    // for each component, if rejecting more than one...
    const Array<int> icaChans = icaReader->settings.enabledChannels;
    jassert(!icaChans.isEmpty()); // (should have errored when running ICA)
    
    int nSamps = getNumSamples(icaChans[0]);
    componentBuffer.clear(0, 0, nSamps);

    // unmix into the components
    for (int c = 0; c < icaChans.size(); ++c)
    {
        int chan = icaChans[c];
        componentBuffer.addFrom(0, 0, buffer, chan, 0, nSamps, icaReader->unmixing(0, c));
    }

    // remix and subtract
    for (int c = 0; c < icaChans.size(); ++c)
    {
        int chan = icaChans[c];
        buffer.addFrom(chan, 0, componentBuffer, 0, 0, nSamps, -icaReader->mixing(c, 0));
    }
}


void ICANode::updateSettings()
{
    // have to wait till here to create the ICARunner because the editor and canvas must exist
    icaRunner = new ICARunner(*this);

    int nChans = getNumInputs();

    componentBuffer.setSize(nChans, componentBuffer.getNumSamples());

    // refresh subprocessor data
    uint32 newSubProc = 0;
    subProcData.clear();
    subProcInfo.clear();

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

        auto subProcEntry = subProcData.find(sourceFullId);
        if (subProcEntry == subProcData.end()) // not found
        {
            SubProcData data;
            data.Fs = chan->getSampleRate();
            data.dsStride = jmax(int(data.Fs / icaTargetFs), 1);
            data.dsOffset = 0;
            data.channelInds.add(c);
            data.dataCache = nullptr;

            subProcData[sourceFullId] = data;

            SubProcInfo info;
            info.sourceID = sourceID;
            info.subProcIdx = subProcIdx;
            info.sourceName = chan->getSourceName();
            subProcInfo[sourceFullId] = info;
        }
        else
        {
            subProcEntry->second.channelInds.add(c);
        }
    }

    currSubProc = newSubProc;

    // create the dataCaches
    int dataCacheIndex = 0;
    int dataCacheSize = dataCaches.size();
    for (auto& subProcEntry : subProcData)
    {
        int nChans = subProcEntry.second.channelInds.size();

        if (dataCacheIndex < dataCacheSize)
        {
            // reuse existing data cache object
            AudioBufferFifo* data = dataCaches[dataCacheIndex];
            jassert(data != nullptr);

            subProcEntry.second.dataCache = data;
            AudioBufferFifo::LockHandle dataHandle(*data);
            dataHandle.resetWithSize(nChans, icaSamples);
        }
        else
        {
            // create new data cache object
            subProcEntry.second.dataCache = dataCaches.add(new AudioBufferFifo(nChans, icaSamples));
        }

        ++dataCacheIndex;
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
    for (AudioBufferFifo* cache : dataCaches)
    {
        if (cache != nullptr)
        {
            AudioBufferFifo::LockHandle hCache(*cache);
            hCache.resizeKeepingData(icaSamples);
        }
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
        return nothingToCollect;
    }

    return subProcData.at(currSubProc).dataCache->getPctFull();
}

/****  ICARunner ****/

const String ICANode::ICARunner::inputFilename("input.floatdata");
const String ICANode::ICARunner::configFilename("binica.sc");
const String ICANode::ICARunner::weightFilename("output.wts");
const String ICANode::ICARunner::sphereFilename("output.sph");

ICANode::ICARunner::ICARunner(ICANode& proc)
    : ThreadWithProgressWindow  ("Preparing...", false, true, 10000, String(), proc.getCanvas())
    , processor                 (proc)
    , progress                  (0)
    , pb                        (progress)
{}

void ICANode::ICARunner::run()
{
    setStatusMessage("Preparing...");

    // collect current settings

    settings.subProc = processor.currSubProc;

    if (settings.subProc == 0)
    {
        reportError("No subprocessor selected");
        return;
    }

    // enabled channels = intersection of active channels and channels of current subprocessor
    settings.enabledChannels = processor.getEditor()->getActiveChannels();
    const Array<int>& subProcChans = processor.subProcData[settings.subProc].channelInds;
    settings.enabledChannels.removeValuesNotIn(subProcChans);

    if (settings.enabledChannels.isEmpty())
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
        baseDir = File::getSpecialLocation(File::hostApplicationPath);
    }

    // create a subdirectory for files, which we want to make sure doesn't exist yet
    while (true)
    {
        if (threadShouldExit()) { return; }

        String time = Time::getCurrentTime().toString(true, true, true, true);
        File outDir = baseDir.getChildFile("ICA_" + time + processor.icaDirSuffix);
        if (!outDir.isDirectory())
        {
            Result res = outDir.createDirectory();
            if (res.failed())
            {
                reportError("Failed to make output directory ("
                    + res.getErrorMessage() + ")");
                return;
            }

            settings.outputDir = outDir;
            break;
        }
    }

    int nSamples = prepareICA();
    if (nSamples == 0 || threadShouldExit()) { return; }
    if (!performICA(nSamples) || threadShouldExit()) { return; }
    processResults();
}

void ICANode::ICARunner::threadComplete(bool userPressedCancel)
{
    // remove progress bar
    getAlertWindow()->removeCustomComponent(0);
}

int ICANode::ICARunner::prepareICA()
{
    setStatusMessage("Collecting data for ICA...");
    progress = 0;
    getAlertWindow()->addCustomComponent(&pb);

    AudioBufferFifo* dataCache = processor.subProcData[settings.subProc].dataCache;
    jassert(dataCache != nullptr);

    File inputFile = settings.outputDir.getChildFile(inputFilename);

    int nWritten = 0;
    bool written = false;
    while (!written)
    {
        // wait for cache to fill up
        while (!dataCache->isFull())
        {
            if (threadShouldExit()) { return 0; }

            progress = dataCache->getPctFull().getValue();

            wait(100);
        }

        while (!written)
        {
            if (threadShouldExit()) { return 0; }

            // shouldn't be contentious since the cache is supposedly full,
            // but avoid blocking with a try lock just in case
            AudioBufferFifo::TryLockHandle hData(*dataCache);
            if (!hData.isLocked())
            {
                wait(100);
                continue;
            }

            // ok, we have the lock. so is the buffer really full or did the
            // length get increased at the last minute?
            if (!dataCache->isFull())
            {
                // start over on waiting for it to fill up
                break;
            }

            // alright, it's really full, we can write it out
            Result writeRes = hData.writeChannelsToFile(inputFile, settings.enabledChannels);
            if (writeRes.wasOk())
            {
                written = true;
                nWritten = dataCache->getNumSamples();
            }
            else
            {
                reportError("Failed to write data to input file ("
                    + writeRes.getErrorMessage() + ")");
                return 0;
            }
        }
    }

    progress = 1;
    getAlertWindow()->removeCustomComponent(0);
    return nWritten;
}

bool ICANode::ICARunner::performICA(int nSamples)
{
    setStatusMessage("Running ICA...");

    // Write config file. For now, not configurable, but maybe can be in the future.
    File configFile = settings.outputDir.getChildFile(configFilename);

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
        configStream << "chans " << settings.enabledChannels.size() << '\n';
        configStream << "frames " << nSamples << '\n';
        configStream << "WeightsOutFile " << weightFilename << '\n';
        configStream << "SphereFile" << sphereFilename << '\n';
        configStream << "maxsteps 512\n";
        configStream << "posact off\n";
        configStream << "annealstep 0.98\n";
        configStream.flush();

        Result status = configStream.getStatus();
        if (status.failed())
        {
            reportError("Failed to write to config file ("
                + status.getErrorMessage() + ")");
            return false;
        }
    }
    
    // do it!
    ICAProcess proc(configFile);

    while (proc.isRunning())
    {
        if (threadShouldExit()) { return false; }
        wait(200);
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

    return true;
}

bool ICANode::ICARunner::processResults()
{
    setStatusMessage("Processing ICA results...");

    // load weight and sphere matrices into Eigen Map objects
    int size = settings.enabledChannels.size();
    int sizeSq = size * size;

    HeapBlock<float> weightBlock(sizeSq);
    File weightFile = settings.outputDir.getChildFile(weightFilename);

    Result res = readOutput(weightFile, weightBlock, sizeSq);
    if (res.failed())
    {
        reportError(res.getErrorMessage());
        return false;
    }

    if (threadShouldExit()) { return false; }

    HeapBlock<float> sphereBlock(sizeSq);
    File sphereFile = settings.outputDir.getChildFile(sphereFilename);

    res = readOutput(sphereFile, sphereBlock, sizeSq);
    if (res.failed())
    {
        reportError(res.getErrorMessage());
        return false;
    }

    if (threadShouldExit()) { return false; }

    Eigen::Map<Eigen::MatrixXf> weights(weightBlock.getData(), size, size);
    Eigen::Map<Eigen::MatrixXf> sphere(sphereBlock.getData(), size, size);

    // now just need to convert this to mixing and unmixing

    // normalize sphere matrix by largest singular value
    Eigen::BDCSVD<decltype(sphere)> svd(sphere);
    Eigen::MatrixXf normSphere = sphere / svd.singularValues()(0);

    if (threadShouldExit()) { return false; }

    RWSync::WritePtr<ICAOutput> icaWriter(processor.icaCoefs);
    jassert(icaWriter.isValid());

    icaWriter->settings = settings;
    icaWriter->unmixing = weights * normSphere;
    icaWriter->mixing = icaWriter->unmixing.inverse();

    icaWriter.pushUpdate();

    return true;
}

Result ICANode::ICARunner::readOutput(const File& source, HeapBlock<float>& dest, int numel)
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

    if (stream.read(dest, numel * sizeof(float)) != numel || !stream.isExhausted())
    {
        Result status = stream.getStatus();
        if (status.wasOk())
        {
            return Result::fail(fn + " has incorrect length");
        }
        else
        {
            return Result::fail("Failed to read " + fn 
                + " (" + status.getErrorMessage() + ")");
        }
    }

    return Result::ok();
}

void ICANode::ICARunner::reportError(const String& whatHappened)
{
    setStatusMessage("Error: " + whatHappened);
    CoreServices::sendStatusMessage("ICA failed: " + whatHappened);
    sleep(1500);
}

