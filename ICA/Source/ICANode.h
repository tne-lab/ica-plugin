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

namespace ICA
{
    using Matrix = Eigen::MatrixXf;
    using MatrixMap = Eigen::Map<Eigen::MatrixXf>;
    using MatrixRef = Eigen::Ref<Eigen::MatrixXf>;
    using MatrixConstRef = const Eigen::Ref<const Eigen::MatrixXf>&;

    // to cache input data to be used to compute ICA
    // modifications must be done through a handle which is secured by a mutex.
    class AudioBufferFifo
    {
    public:
        explicit AudioBufferFifo(int numChans = 0, int numSamps = 0);

        int getNumSamples() const;

        const Value& getPctFull() const;

        class Handle
        {
        protected:
            Handle(AudioBufferFifo& fifoIn);

        public:
            bool isFull() const;

            void reset();
            void resetWithSize(int numChans, int numSamps);

            // copy one sample of another audio buffer
            void copySample(const AudioSampleBuffer& source, const SortedSet<int>& channels, int sample);

            // changes the size of the buffer while keeping as much data as possible
            void resizeKeepingData(int numSamps);

            // write all samples of the given channels to the given file in column-major order.
            // expects that the FIFO is already full.
            Result writeChannelsToFile(const File& file, const SortedSet<int>& channels);

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
        Value pctFull; // for display - rounded down to int
        
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioBufferFifo);
    };

    struct SubProcInfo
    {
        uint16 sourceID;
        uint16 subProcIdx;
        String sourceName;
        StringArray channelNames;

        // source ID and subproc index uniquely identify the subprocessor
        bool operator==(const SubProcInfo& other) const;

        // display name for the ComboBox
        operator String() const;

        JUCE_LEAK_DETECTOR(SubProcInfo);
    };

    // an ICAOperation operates on a specific set of channels within a given subprocessor,
    // but should be agnostic to the identity of the subprocessor and of the channels within it.
    // this allows it to be loaded in similar but nonidentical signal chains.
    struct ICAOperation
    {
        Matrix mixing;
        Matrix unmixing;
        SortedSet<int> enabledChannels;   // of this subprocessor's channels, which to include in ica
        SortedSet<int> rejectedComponents;

        inline bool isNoop() const
        {
            return enabledChannels.isEmpty();
        }

        JUCE_LEAK_DETECTOR(ICAOperation);
    };

    class ICANode : public GenericProcessor, public Thread
    {
    public:
        ICANode();
	~ICANode();

        bool hasEditor() const { return true; }
        AudioProcessorEditor* createEditor() override;

        // returns false on error, including if ICA is already running
        bool startICA();

        // replace any current ICA transformation with a dummy one that does nothing
        void resetICA(uint32 subProc, bool block = false);

        Result loadICA(const File& configFile);

        // clear the data cache for this subproc and start over at 0%
        void resetCache(uint32 subProc);

        // not used currently:
        //bool enable() override;

        bool disable() override;

        void process(AudioSampleBuffer& buffer) override;

        void updateSettings() override;

        // Thread function - does an ICA run.
        void run() override;

        void saveCustomParametersToXml(XmlElement* parentElement) override;

        void loadCustomParametersFromXml() override;

        // access stuff

        Component* getCanvas() const;

        float getTrainDurationSec() const;
        void setTrainDurationSec(float dur);

        String getDirSuffix() const;
        void setDirSuffix(const String& suffix);

        const std::map<uint32, SubProcInfo>& getSubProcInfo() const;
        uint32 getCurrSubProc() const;
        void setCurrSubProc(uint32 fullId);

        // returns null if no current subproc
        const StringArray* getCurrSubProcChannelNames() const;

        // also returns a reference to the value, so it can be identified in the callback.
        const Value& addPctFullListener(Value::Listener* listener);

        // also returns a reference to the value, so it can be identified in the callback.
        const Value& addConfigPathListener(Value::Listener* listener);

        // same deal
        const Value& addICARunningListener(Value::Listener* listener);

        // returns null if there is no input or no real operation (i.e. operation is a no-op)
        // otherwise, returns the current operation and makes a lock in the passed-in pointer for its mutex.
        const ICAOperation* readICAOperation(ScopedPointer<ScopedReadLock>& lock) const;

        // similar to above
        ICAOperation* writeICAOperation(ScopedPointer<ScopedWriteLock>& lock) const;

        // get root directory of ICA results
        static File getICABaseDir();

    private:
        /**** member types ****/

        struct SubProcData
        {
            float Fs;
            int dsStride; // = Fs / icaTargetFs (rounded to an int)
            int dsOffset;

            SortedSet<int> channelInds; // (indices in this processor)

            // for colllecting data for ICA during acquisition
            ScopedPointer<AudioBufferFifo> dataCache;

            ReadWriteLock icaMutex; // controls below variables
            ScopedPointer<ICAOperation> icaOp;
            Value icaConfigPath;    // full path of current ICA transformatiion config file, if any
        };

        // for temporary storage while calculating ICA operation
        struct ICARunInfo
        {
            uint32 subProc;
            int nSamples = 0;
            int nChannels = 0;
            File config;
            File weight;
            File sphere;
            ScopedPointer<ICAOperation> op;
        };

        /***** nonstatic member functions ****/

        // Populate the info struct
        Result prepareICA(ICARunInfo& info);

        // Write data for ICA to input.floatdata file
        Result writeCacheData(ICARunInfo& info);
        
        // Call the binica executable on our sample data
        Result performICA(ICARunInfo& info);

        // Read in output from binica and compute fields of ICAOutput
        Result processResults(ICARunInfo& info);

        Result setRejectedCompsBasedOnCurrent(ICARunInfo& info);

        // Tries to set the ICA operation described in info (i.e. on the correct
        // subprocessor, with the matrices, enabled channels, and rejected components
        // in the pointed-to operation.
        //
        // Fails if the target subprocessor is no longer present.
        //
        // If the rejected components are not/no longer valid due to the number of
        // channels being too small, does not fail but defaults to rejecting the
        // first component.
        Result setNewICAOp(ICARunInfo& info);

        // Load ICA for a specific subprocessor.
        // If rejectSet is non-null, it will be *swapped* into the resulting operation's
        // rejected components, if possible.
        Result loadICA(const File& configFile, uint32 subProc, const SortedSet<int>* rejectSet = nullptr);
        
        // For use when loading data. Uses .sc config file to fill in
        // other information (including the transformation itself)
        Result populateInfoFromConfig(ICARunInfo& info);

        /**** static member functions ****/

        // Helper to save processed ICA transformation
        static Result saveMatrix(const File &dest, MatrixConstRef mat);

        // Helper to read ICA output
        static Result readMatrix(const File& source, MatrixRef dest);

        // for encoding matrices in XML
        static void saveMatrixToXml(XmlElement* xml, MatrixConstRef mat);
        static Result readMatrixFromXml(const XmlElement* xml, MatrixRef dest);

        // for encoding sets of channels etc. in XML
        static String intSetToString(const SortedSet<int>& set);
        static SortedSet<int> stringToIntSet(const String& string);


        /**** nonstatic data members ****/

        int icaSamples; // updated from editor

        String icaDirSuffix; // updated from editor

        // ordered so that combobox is consistent/goes in lexicographic order of subproc
        std::map<uint32, SubProcInfo> subProcInfo;
        std::map<uint32, SubProcData> subProcData;

        // relevant to state of editor and canvas
        uint32 currSubProc;      // full source ID of selected subproc
        Value currICAConfigPath; // full path to .sc file
        Value currPctFull;
        Value icaRunning;

        // temporary storage for ICA components
        AudioSampleBuffer componentBuffer;
        

        /**** static constants ****/

        // frequency at which samples are taken for ICA
        static const float icaTargetFs;

        // start of line containing enabled channels hint in binica.sc files
        static const String chanHintPrefix;

        static const String inputFilename;
        static const String configFilename;
        static const String weightFilename;
        static const String sphereFilename;
        static const String mixingFilename;
        static const String unmixingFilename;

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


    // not sure why this doesn't already exist
    // see: juce::GenericScopedTryLock

    class ScopedReadTryLock
    {
    public:
        explicit ScopedReadTryLock(const ReadWriteLock& lock) noexcept;
        ~ScopedReadTryLock() noexcept;

        bool isLocked() const noexcept;

    private:
        const ReadWriteLock& lock_;
        const bool lockWasSuccessful;

        JUCE_DECLARE_NON_COPYABLE(ScopedReadTryLock);
    };

    class ScopedWriteTryLock
    {
    public:
        explicit ScopedWriteTryLock(const ReadWriteLock& lock) noexcept;
        ~ScopedWriteTryLock() noexcept;

        bool isLocked() const noexcept;

    private:
        const ReadWriteLock& lock_;
        const bool lockWasSuccessful;

        JUCE_DECLARE_NON_COPYABLE(ScopedWriteTryLock);
    };
}

#endif // ICA_NODE_H_DEFINED
