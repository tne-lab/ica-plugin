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

#include "../ICANode.h"

#include <Windows.h>

using namespace ICA;

class ICAProcess::NativeICAProcess
{
public:
    NativeICAProcess(const String& settingsPath)
    {
        SECURITY_ATTRIBUTES securityAtts = { 0 };
        securityAtts.nLength = sizeof(securityAtts);
        securityAtts.bInheritHandle = TRUE;

        settingsFile = CreateFile(settingsPath.toWideCharPointer(), GENERIC_READ,
            0, &securityAtts, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);

        if (settingsFile == INVALID_HANDLE_VALUE)
        {
            failed = true;
            settingsFile = 0;
            return;
        }

        STARTUPINFOW startupInfo = { 0 };
        startupInfo.cb = sizeof(startupInfo);

        startupInfo.dwFlags = STARTF_USESTDHANDLES;
        startupInfo.hStdInput = settingsFile;
        startupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        static const String binicaExe =
            File::getSpecialLocation(File::hostApplicationPath)
            .getParentDirectory().getChildFile("binica.exe").getFullPathName();
            
        failed = !CreateProcess(binicaExe.toWideCharPointer(), nullptr,
            nullptr, nullptr, TRUE, CREATE_UNICODE_ENVIRONMENT,
            nullptr, nullptr, &startupInfo, &processInfo);
    }

    ~NativeICAProcess()
    {
        // this just closes the handles, doesn't terminate the process
        for (HANDLE h : { processInfo.hThread, processInfo.hProcess, settingsFile })
        {
            if (h != 0)
            {
                CloseHandle(h);
                h = 0;
            }
        }
    }

    bool isRunning() const
    {
        if (failed)
        {
            return false;
        }

        if (processInfo.hProcess == 0) // hasn't started yet nor failed
        {
            return true;
        }

        return WaitForSingleObject(processInfo.hProcess, 0) != WAIT_OBJECT_0;
    }

    uint32 getExitCode() const
    {
        if (failed)
        {
            return 1;
        }

        if (processInfo.hProcess == 0) // hasn't started yet nor failed
        {
            return 0;
        }

        DWORD exitCode = 0;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        return uint32(exitCode);
    }

    bool failed = false;

private:
    HANDLE settingsFile = 0;
    PROCESS_INFORMATION processInfo = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativeICAProcess);
};


ICAProcess::ICAProcess(const String& settingsPath)
    : nativeProcess(new NativeICAProcess(settingsPath))
{}

ICAProcess::~ICAProcess()
{}

bool ICAProcess::isRunning() const
{
    return nativeProcess->isRunning();
}

bool ICAProcess::failedToRun() const
{
    return nativeProcess->failed;
}

uint32 ICAProcess::getExitCode() const
{
    return nativeProcess->getExitCode();
}