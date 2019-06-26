#ifndef ICA_NODE_H_DEFINED
#define ICA_NODE_H_DEFINED

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

#include <ProcessorHeaders.h>

#include <map>
#include <Eigen/Dense>
#include <RWSync/RWSyncContainer.h>

namespace ICA
{
    // to cache input data to be used to compute ICA
    // operations must be done through a handle which is secured by a mutex.
    class AudioBufferFifo
    {
    private:
        struct Handle
        {
            AudioBufferFifo& fifo;

            void reset();
            void resetWithSize(int numChans, int numSamps);

            // copy one sample of another audio buffer
            void copySample(const AudioSampleBuffer& source, const Array<int>& channels, int sample);

            // changes the size of the buffer while keeping as much data as possible
            void resizeKeepingData(int numSamps);

            // write all samples of the given channels to the given file in column-major order.
            // expects that the FIFO is already full.
            Result writeChannelsToFile(const File& file, const Array<int>& channels);
        };

    public:
        class LockHandle : public Handle, public ScopedLock
        {
        public:
            LockHandle(AudioBufferFifo& fifoIn);
        };

        class TryLockHandle : public Handle, public ScopedTryLock
        {
        public:
            TryLockHandle(AudioBufferFifo& fifoIn);
        };

        AudioBufferFifo(int numChans, int numSamps);

        const Value& getPctWritten() const;

        bool isFull() const;

    private:
        ScopedPointer<AudioSampleBuffer> data;
        CriticalSection mutex;

        int startPoint;
        int numWritten;
        Value pctWritten; // for display - nearest int
        Atomic<bool> full;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBufferFifo);
    };

    class ICANode : public GenericProcessor
    {
    public:
        ICANode();
        ~ICANode();

        bool hasEditor() const { return true; }
        AudioProcessorEditor* createEditor() override;

        /** Optional method that informs the GUI if the processor is ready to function. If false acquisition cannot start. Defaults to true */
        //bool isReady();

        //bool enable() override;

        //bool disable() override;

        void process(AudioSampleBuffer& buffer) override;

        /** The method that standard controls on the editor will call.
        It is recommended that any variables used by the "process" function
        are modified only through this method while data acquisition is active. */
        //void setParameter(int parameterIndex, float newValue) override;

        /** Saving custom settings to XML. */
        //void saveCustomParametersToXml(XmlElement* parentElement) override;

        /** Load custom settings from XML*/
        //void loadCustomParametersFromXml() override;

        /** Optional method called every time the signal chain is refreshed or changed in any way.

        Allows the processor to handle variations in the channel configuration or any other parameter
        passed down the signal chain. The processor can also modify here the settings structure, which contains
        information regarding the input and output channels as well as other signal related parameters. Said
        structure shouldn't be manipulated outside of this method.

        */
        void updateSettings() override;

    private:

        static const float icaTargetFs;
        int icaSamples; // updated from editor

        String icaDirSuffix; // updated from editor

        struct SubProcInfo
        {
            String sourceName;
            uint16 sourceID;
            uint16 subProcIdx;

            float Fs;
            int dsStride; // = Fs / icaTargetFs (rounded to an int)

            Array<int> channelInds; // (indices in this processor)

            ScopedPointer<AudioBufferFifo> dataCache;
        };

        std::unordered_map<uint32, SubProcInfo> subProcInfo;
        uint32 currSubProc; // full source ID

        int dsOffset = 0;

        AudioSampleBuffer componentBuffer;

        // for writing input data for binica
        static const size_t dataBlocksize = 1024;

        //enum { idle, collecting, processing } state = idle;

        // info specific to an ICA run
        struct ICASettings
        {           
            Array<int> enabledChannels;
            File outputDir;
        };

        class ICARunner : public ThreadWithProgressWindow
        {
        public:
            ICARunner(ICANode& proc);

            void run() override;

            void threadComplete(bool userPressedCancel) override;

        private:
            // Subroutines of run(). Each returns false if it is interrupted
            // or otherwise does not complete successfully.

            // Collect data from the process thread and write to a file
            bool prepareICA();

            // Call the binica executable on our sample data
            bool performICA();

            // Read in output from binica and compute fields of ICAOutput
            bool processResults();

            void reportError(const String& whatHappened);

            ICASettings settings;
            ICANode& processor;

            double progress;
            ProgressBar pb; // hold it so we can control it manually

            static const String inputFilename;
            static const String configFilename;
            static const String wtsFilename;
            static const String sphFilename;
        };

        // for calculating the selection matrix
        struct ICAOutput
        {
            Eigen::MatrixXf mixing;
            Eigen::MatrixXf unmixing;
            ICASettings settings;
        };

        // writer: ICARunner
        // reader: TransformationUpdater
        RWSync::FixedContainer<ICAOutput> icaCoefs;

        // writer: GUI thread
        // reader: TransformationUpdater
        RWSync::FixedContainer<Eigen::VectorXf> componentsToKeep;

        class TransformationUpdater : public Thread
        {
        public:
            TransformationUpdater(ICANode& proc);

            void run() override;

        private:
            ICANode& processor;
        };

        // for applying the transformation in process()
        struct ICATransformation
        {
            Eigen::MatrixXf transformation;
            ICASettings settings;
        };

        // writer: TransformationUpdater
        // reader: process thread
        RWSync::FixedContainer<ICATransformation> icaTransformation;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICANode);
    };


    // Manages the process that runs binica.
    // Mostly based on juce::ChildProcess, but adding the ability to redirect stdin,
    // which binica requires.
    // Creating an instance automatically starts the process.
    // All stdout and stderr goes to the console.
    class ICAProcess
    {
        // pimpl
        class NativeICAProcess;
        ScopedPointer<NativeICAProcess> nativeProcess;

    public:
        ICAProcess(const File& configFile);
        ~ICAProcess();

        bool isRunning() const;

        // check whether the process didn't even get started
        bool failedToRun() const;

        // precondition: isRunning() and failedToRun() are false
        // (if failed to run, will return 1 to indicate generic failure)
        int32 getExitCode() const;

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICAProcess);
    };
}

#endif // ICA_NODE_H_DEFINED