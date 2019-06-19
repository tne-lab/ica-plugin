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

#ifdef WIN32
#include <Windows.h>
#endif

using namespace ICA;

//Change all names for the relevant ones, including "Processor Name"
ICANode::ICANode()
    : GenericProcessor  ("ICA")
    , selectionMatrix   (2, 2)
    , icaThread         (*this)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);

    selectionMatrix(0, 0) = 3;
    selectionMatrix(1, 0) = 2.5;
    selectionMatrix(0, 1) = -1;
    selectionMatrix(1, 1) = selectionMatrix(1, 0) + selectionMatrix(0, 1);
    std::cout << selectionMatrix << std::endl;

    ChildProcess icaProc;
    char output[100];
    if (!icaProc.start("binica.exe < pre_binica.sc"))
    {
        jassertfalse;
    }

    while (icaProc.isRunning())
    {
        int numRead;
        if ((numRead = icaProc.readProcessOutput(output, 99)) > 0)
        {
            output[numRead] = '\0';
            std::cout << output;
        }
    }
    uint32 exitCode = icaProc.getExitCode();
    std::cout << "binica returned " << static_cast<int32>(exitCode) << std::endl;
}

ICANode::~ICANode()
{

}

AudioProcessorEditor* ICANode::createEditor()
{
    editor = new ICAEditor(this);
    return editor;
}

bool ICANode::enable()
{
    icaThread.startThread();
    return isEnabled;
}

bool ICANode::disable()
{
    return icaThread.stopThread(500);
}

void ICANode::process(AudioSampleBuffer& buffer)
{
	/** 
	If the processor needs to handle events, this method must be called onyl once per process call
	If spike processing is also needing, set the argument to true
	*/
	//checkForEvents(false);
	int numChannels = getNumOutputs();

	for (int chan = 0; chan < numChannels; chan++)
	{
		int numSamples = getNumSamples(chan);
		int64 timestamp = getTimestamp(chan);

		//Do whatever processing needed
	}
    
    
}


// ICARunner
ICANode::ICARunner::ICARunner(ICANode& creatorNode)
    : Thread    ("ICA Thread")
    , node      (creatorNode)
    , process   (nullptr)
{}

ICANode::ICARunner::~ICARunner()
{
    delete process;
}

void ICANode::ICARunner::run()
{
}


#ifdef WIN32

class ICANode::ICARunner::NativeICAProcess
{
public:
    NativeICAProcess(const String& path)
        : ok                (false)
        , settingsPath      (path)
        , processReadPipe   (0)
        , processWritePipe  (0)
        , settingsReadPipe  (0)
        , settingsWritePipe (0)
    {
        SECURITY_ATTRIBUTES securityAtts = { 0 };
        securityAtts.nLength = sizeof(securityAtts);
        securityAtts.bInheritHandle = TRUE;

        if (CreatePipe(&processReadPipe, &processWritePipe, &securityAtts, 0)
            && SetHandleInformation(processReadPipe, HANDLE_FLAG_INHERIT, 0)
            && CreatePipe(&settingsReadPipe, &settingsWritePipe, &securityAtts, 0)
            && SetHandleInformation(settingsWritePipe, HANDLE_FLAG_INHERIT, 0))
        {
            STARTUPINFOW startupInfo = { 0 };
            startupInfo.cb = sizeof(startupInfo);

            startupInfo.hStdInput = settingsReadPipe;
            startupInfo.hStdOutput = processWritePipe;
            startupInfo.hStdError = processWritePipe;
            startupInfo.dwFlags = STARTF_USESTDHANDLES;

            if (CreateProcess(L"binica.exe", nullptr,
                nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                nullptr, nullptr, &startupInfo, &processInfo) == FALSE)
            {
                return;
            }

            ok = pipeSettingsToICA();
        }
    }

    ~NativeICAProcess()
    {

    }

    bool ok;

private:

    bool pipeSettingsToICA()
    {
        // source: https://docs.microsoft.com/en-us/windows/desktop/procthread/creating-a-child-process-with-redirected-input-and-output

        HANDLE settingsFile = CreateFile(settingsPath.toWideCharPointer(), GENERIC_READ,
            0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);

        if (settingsFile == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        static const DWORD bufsize = 4096;
        DWORD numRead, numWritten;
        CHAR buf[bufsize];
        BOOL success = FALSE;

        while (true)
        {
            success = ReadFile(settingsFile, buf, bufsize, &numRead, nullptr);
            if (!success || numRead == 0)
            {
                break;
            }

            success = WriteFile(settingsWritePipe, buf, numRead, &numWritten, nullptr);
            if (!success)
            {
                break;
            }
        }

        // close the pipe handle so the child process stops reading
        if (!CloseHandle(settingsWritePipe))
        {
            return false;
        }


    }

    const String& settingsPath;
    HANDLE processReadPipe, processWritePipe, settingsReadPipe, settingsWritePipe;
    PROCESS_INFORMATION processInfo;
};

#endif