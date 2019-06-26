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

void AudioBufferFifo::Handle::reset()
{
    fifo.startPoint = 0;
    fifo.numWritten = 0;
    fifo.pctWritten = 0;
    fifo.full = (fifo.data->getNumSamples() == 0);
}

void AudioBufferFifo::Handle::resetWithSize(int numChans, int numSamps)
{
    jassert(numChans >= 0 && numSamps >= 0);
    fifo.data->setSize(numChans, numSamps);
    reset();
}


void AudioBufferFifo::Handle::copySample(const AudioSampleBuffer& source, 
    const Array<int>& channels, int sample)
{
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

    fifo.full = (fifo.numWritten == numSamps);
    fifo.pctWritten = int(double(fifo.numWritten) / numSamps + 0.5);
}


void AudioBufferFifo::Handle::resizeKeepingData(int numSamps)
{
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

    fifo.full = (fifo.numWritten == numSamps);
    fifo.pctWritten = int(double(fifo.numWritten) / numSamps + 0.5);
}


Result AudioBufferFifo::Handle::writeChannelsToFile(const File& file, const Array<int>& channels)
{
    FileOutputStream stream(file);
    if (!stream.openedOk())
    {
        return stream.getStatus();
    }

    jassert(fifo.full.get()); // should only be called when the FIFO is full...
    
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


AudioBufferFifo::LockHandle::LockHandle(AudioBufferFifo& fifoIn)
    : Handle        ({ fifoIn })
    , ScopedLock    (fifoIn.mutex)
{}

AudioBufferFifo::TryLockHandle::TryLockHandle(AudioBufferFifo& fifoIn)
    : Handle        ({ fifoIn })
    , ScopedTryLock (fifoIn.mutex)
{}

AudioBufferFifo::AudioBufferFifo(int numChans, int numSamps)
    : data          (new AudioSampleBuffer(numChans, numSamps))
    , startPoint    (0)
    , numWritten    (0)
    , full          (numSamps == 0)
{
    pctWritten = 0;
}

const Value& AudioBufferFifo::getPctWritten() const
{
    return pctWritten;
}

bool AudioBufferFifo::isFull() const
{
    return full.get();
}


/*****  ICANode *****/

const float ICANode::icaTargetFs = 300.0f;

ICANode::ICANode()
    : GenericProcessor  ("ICA")
{
    setProcessorType(PROCESSOR_TYPE_FILTER);
}

ICANode::~ICANode()
{

}

AudioProcessorEditor* ICANode::createEditor()
{
    editor = new ICAEditor(this);
    return editor;
}

//bool ICANode::enable()
//{
//    startThread();
//    return isEnabled;
//}
//
//bool ICANode::disable()
//{
//    return stopThread(500);
//}

void ICANode::process(AudioSampleBuffer& buffer)
{
    // should only happen on the first buffer, since they should all
    // be the same length during acquisition
    int bufferCapacity = componentBuffer.getNumSamples();
    if (buffer.getNumSamples() > componentBuffer.getNumSamples())
    {
        componentBuffer.setSize(nChans, bufferCapacity);
    }

    // send data if we are collecting
    
    if ()
    {
        RWSync::WritePtr<MemoryBlock> dataWriter(sharedData);
        for (int chan : )
        {

        }
    }

	int numChannels = getNumOutputs();

	for (int chan = 0; chan < numChannels; chan++)
	{
		int numSamples = getNumSamples(chan);
		int64 timestamp = getTimestamp(chan);

		//Do whatever processing needed
	}


}


void ICANode::updateSettings()
{
    int nChans = getNumInputs();
    subProcInfo.clear();

    for (int c = 0; c < nChans; ++c)
    {
        const DataChannel* chan = getDataChannel(c);
        uint16 sourceID = chan->getSourceNodeID();
        uint16 subProcIdx = chan->getSubProcessorIdx();
        uint32 sourceFullId = getProcessorFullId(sourceID, subProcIdx);

        auto subProcEntry = subProcInfo.find(sourceFullId);
        if (subProcEntry == subProcInfo.end()) // not found
        {
            SubProcInfo info;
            info.sourceName = chan->getSourceName();
            info.sourceID = sourceID;
            info.subProcIdx = subProcIdx;
            info.Fs = chan->getSampleRate();
            info.dsStride = jmax(int(info.Fs / icaTargetFs), 1);
            info.channelInds.add(c);

            subProcInfo[sourceFullId] = info;
        }
        else
        {
            subProcEntry->second.channelInds.add(c);
        }
    }

    // create the dataCaches
    for (auto& subProcEntry : subProcInfo)
    {
        int nChans = subProcEntry.second.channelInds.size();
        subProcEntry.second.dataCache = new AudioBufferFifo(nChans, icaSamples);
    }
}


/****  ICARunner ****/

const String ICANode::ICARunner::inputFilename("input.floatdata");
const String ICANode::ICARunner::configFilename("binica.sc");
const String ICANode::ICARunner::wtsFilename("output.wts");
const String ICANode::ICARunner::sphFilename("output.sph");

ICANode::ICARunner::ICARunner(ICANode& proc)
    : ThreadWithProgressWindow  ("Preparing...", false, true, 10000, String(), proc.getEditor())
    , processor                 (proc)
    , progress                  (0)
    , pb                        (progress)
{}

void ICANode::ICARunner::run()
{
    setStatusMessage("Preparing...");

    // inform the editor
    auto ed = static_cast<ICAEditor*>(processor.getEditor());
    ed->icaStarting();

    // collect current settings

    // enabled channels = intersection of active channels and channels of current subprocessor
    settings.enabledChannels = ed->getActiveChannels();
    const Array<int>& subProcChans = processor.subProcInfo[processor.currSubProc].channelInds;
    settings.enabledChannels.removeValuesNotIn(subProcChans);

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

    String dirSuffix = ed->getDirSuffix();

    // create a subdirectory for files, which we want to make sure doesn't exist yet
    while (true)
    {
        if (threadShouldExit()) { return; }

        String time = Time::getCurrentTime().toString(true, true, true, true);
        File outDir = baseDir.getChildFile("ICA_" + time + "_" + dirSuffix);
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

    if (!prepareICA() || threadShouldExit()) { return; }
    if (!performICA() || threadShouldExit()) { return; }
    processResults();
}

bool ICANode::ICARunner::prepareICA()
{
    setStatusMessage("Collecting data for ICA...");
    progress = 0;
    getAlertWindow()->addCustomComponent(&pb);

    FileOutputStream stream(mySettings.outputDir.getChildFile(inputFilename));

    if (!stream.openedOk())
    {
        reportError("Failed to open binica input file");
        return false;
    }

    // when the shared data container is free, reset and resize it
    jassert(processor.channelsForCollection == nullptr);

    int nChannels = mySettings.enabledChannels.size();
    size_t totalBytes = nChannels * mySettings.nSamples * sizeof(float);
    size_t bytesRemaining = totalBytes;
    size_t bytesInBlock = nChannels * ICANode::dataBlocksize * sizeof(float);

    while (!processor.sharedData.map([=](MemoryBlock& mb)
    {
        mb.setSize(bytesInBlock);
    }))
    {
        // should succeed, but maybe we're waiting on a previous write from process()?
        if (threadShouldExit()) { return false; }
        wait(100);
    }

    processor.channelsForCollection = &mySettings.enabledChannels;

    RWSync::ReadPtr<MemoryBlock> dataReader(processor.sharedData);
    if (!dataReader.isValid())
    {
        jassertfalse;
        reportError("RWSync error, data cannot be transferred");
        return false;
    }

    while (bytesRemaining > 0)
    {
        if (threadShouldExit()) { return false; }

        if (dataReader.hasUpdate())
        {
            dataReader.pullUpdate();
            size_t bytesToWrite = jmin(bytesRemaining, bytesInBlock);

            if (!stream.write(dataReader->getData(), bytesToWrite))
            {
                reportError("Failed to write data to input file ("
                    + stream.getStatus().getErrorMessage() + ")");
                return false;
            }

            bytesRemaining -= bytesToWrite;
            progress = (totalBytes - bytesRemaining) / double(bytesRemaining);
        }
        else
        {
            wait(100);
        }
    }
    stream.flush();

    processor.channelsForCollection = nullptr;
    getAlertWindow()->removeCustomComponent(0);
    return true;
}

bool ICANode::ICARunner::performICA()
{
    setStatusMessage("Running ICA...");

    // Write config file. For now, not configurable, but maybe can be in the future.
    File configFile = mySettings.outputDir.getChildFile(configFilename);

    { // scope in which configStream exists
        FileOutputStream configStream(mySettings.outputDir.getChildFile(configFilename));
        if (!configStream.openedOk())
        {
            reportError("Failed to open binica config file");
            return false;
        }

        // skips some settings where the default is ok
        configStream << "# binica config file - for details, see https://sccn.ucsd.edu/wiki/Binica \n";
        configStream << "DataFile " << inputFilename << '\n';
        configStream << "chans " << mySettings.enabledChannels.size() << '\n';
        configStream << "frames " << mySettings.nSamples << '\n';
        configStream << "WeightsOutFile " << wtsFilename << '\n';
        configStream << "SphereFile" << sphFilename << '\n';
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
}

void ICANode::ICARunner::threadComplete(bool userPressedCancel)
{
    // if interrupted while collecting, stop sending data
    processor.channelsForCollection = nullptr;

    // remove progress bar
    getAlertWindow()->removeCustomComponent(0);

    // inform the editor
    static_cast<ICAEditor*>(processor.getEditor())->icaStopping();
}

void ICANode::ICARunner::reportError(const String& whatHappened)
{
    setStatusMessage("Error: " + whatHappened);
    CoreServices::sendStatusMessage("ICA failed: " + whatHappened);
    sleep(1500);
}


ICANode::TransformationUpdater::TransformationUpdater(ICANode& proc)
    : Thread    ("ICA transformation update thread")
    , processor (proc)
{}

void ICANode::TransformationUpdater::run()
{
    RWSync::ReadPtr<ICAOutput> icaReadPtr(processor.icaCoefs);
    RWSync::ReadPtr<Eigen::VectorXf> selectionReadPtr(processor.componentsToKeep);

    if (!icaReadPtr.canRead())
    {
        jassertfalse;
        return;
    }

    int nChans = icaReadPtr->enabledChannels.size();
    Eigen::VectorXf defaultSelection = Eigen::VectorXf::Zero(nChans);
    defaultSelection(0) = 1.0f;

    Eigen::VectorXf* selection;

    if (!selectionReadPtr.canRead() || selectionReadPtr->size() != nChans)
    {
        // Visualizer may not even be in use or is out of date with enabled channels
        // default to just selecting the first component and throwing out the rest
        selection = &defaultSelection;
    }
    else
    {
        selection = selectionReadPtr;
    }

    RWSync::WritePtr<ICATransformation> transWritePtr(processor.icaTransformation);
    transWritePtr->enabledChannels.swapWith(icaReadPtr->enabledChannels);
    transWritePtr->transformation = icaReadPtr->mixing * selection->asDiagonal() * icaReadPtr->unmixing;

    transWritePtr.pushUpdate();
}