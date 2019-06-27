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
#include <atomic>
#include <Eigen/Dense>
#include <RWSync/RWSyncContainer.h>

namespace ICA
{
    // to cache input data to be used to compute ICA
    // modifications must be done through a handle which is secured by a mutex.
    class AudioBufferFifo
    {
    public:
        AudioBufferFifo(int numChans, int numSamps);

        int getNumSamples() const;

        const Value& getPctFull() const;
        bool isFull() const;

        class Handle
        {
        protected:
            Handle(AudioBufferFifo& fifoIn);

        public:
            void reset();
            void resetWithSize(int numChans, int numSamps);

            // copy one sample of another audio buffer
            void copySample(const AudioSampleBuffer& source, const Array<int>& channels, int sample);

            // changes the size of the buffer while keeping as much data as possible
            void resizeKeepingData(int numSamps);

            // write all samples of the given channels to the given file in column-major order.
            // expects that the FIFO is already full.
            Result writeChannelsToFile(const File& file, const Array<int>& channels);

        private:
            // whether operations should be permitted
            virtual bool isValid() const;

            AudioBufferFifo& fifo;
        };

        class LockHandle : public Handle, public ScopedLock
        {
        public:
            LockHandle(AudioBufferFifo& fifoIn);
        };

        class TryLockHandle : public Handle, public ScopedTryLock
        {
        public:
            TryLockHandle(AudioBufferFifo& fifoIn);

        private:
            bool isValid() const override;
        };

    private:

        // updates pctWritten and full based on numWritten
        void updateFullStatus();

        void reset();

        ScopedPointer<AudioSampleBuffer> data;
        CriticalSection mutex;

        int startPoint;
        int numWritten;
        Value pctFull; // for display - nearest int
        std::atomic<bool> full;
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBufferFifo);
    };

    class ICANode : public GenericProcessor
    {
    public:
        ICANode();

        bool hasEditor() const { return true; }
        AudioProcessorEditor* createEditor() override;

        /** Optional method that informs the GUI if the processor is ready to function. If false acquisition cannot start. Defaults to true */
        //bool isReady();

        // returns false on error, including if ICA is already running
        bool startICA();

        //bool enable() override;

        bool disable() override;

        void process(AudioSampleBuffer& buffer) override;

        /** Saving custom settings to XML. */
        //void saveCustomParametersToXml(XmlElement* parentElement) override;

        /** Load custom settings from XML*/
        //void loadCustomParametersFromXml() override;

        void updateSettings() override;

        // access stuff

        Component* getCanvas() const;

        float getTrainDurationSec() const;
        void setTrainDurationSec(float dur);

        String getDirSuffix() const;
        void setDirSuffix(const String& suffix);

        struct SubProcInfo
        {
            uint16 sourceID;
            uint16 subProcIdx;
            String sourceName;

            // display name for the ComboBox
            operator String() const;
        };

        const std::map<uint32, SubProcInfo>& getSubProcInfo() const;
        uint32 getCurrSubProc() const;
        void setCurrSubProc(uint32 fullId);

        const Value& getPctFullValue() const;

    private:

        static const float icaTargetFs;
        int icaSamples; // updated from editor

        String icaDirSuffix; // updated from editor

        // for colllecting data from each subprocessor during acquisition
        struct SubProcData
        {
            float Fs;
            int dsStride; // = Fs / icaTargetFs (rounded to an int)
            int dsOffset;

            Array<int> channelInds; // (indices in this processor)

            AudioBufferFifo* dataCache;
        };

        std::map<uint32, SubProcInfo> subProcInfo;
        std::map<uint32, SubProcData> subProcData;
        uint32 currSubProc; // full source ID

        // what is shown on the editor when there is no subprocessor (no inputs)
        static const Value nothingToCollect;

        // owns the data (allows reuse between updates)
        // should be accessed through a SubProcInfo struct though.
        OwnedArray<AudioBufferFifo> dataCaches;

        // temporary storage for ICA components
        AudioSampleBuffer componentBuffer;

        // info specific to an ICA run
        struct ICASettings
        {
            uint32 subProc;
            Array<int> enabledChannels;
            File outputDir;
        };

        class ICARunner : public ThreadWithProgressWindow
        {
        public:
            ICARunner(ICANode& proc);

            void threadComplete(bool userPressedCancel) override;

            void run() override;

        private:

            // Collect data from the process thread and write to a file.
            // Returns # of samples written, or 0 on failure
            int prepareICA();

            // Call the binica executable on our sample data
            bool performICA(int nSamples);

            // Read in output from binica and compute fields of ICAOutput
            bool processResults();

            // Helper to read ICA output
            static Result readOutput(const File& source, HeapBlock<float>& dest, int numel);

            void reportError(const String& whatHappened);

            ICASettings settings;
            ICANode& processor;

            double progress;
            ProgressBar pb; // hold it so we can control it manually

            static const String inputFilename;
            static const String configFilename;
            static const String weightFilename;
            static const String sphereFilename;
        };

        ScopedPointer<ICARunner> icaRunner;

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
        // TODO, for now just take first component
        //RWSync::FixedContainer<Eigen::VectorXf> componentsToKeep;

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