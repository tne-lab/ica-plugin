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

#include <unistd.h>
#include <cstdio>
#include <sys/types.h>
#include <sys/wait.h>

using namespace ICA;

class ICAProcess::NativeICAProcess
{
public:
    NativeICAProcess(const File& configFilename)
    {
        // open config file
        configFile = fopen(configFilename.getFullPathName().toRawUTF8(), "r");
        if (configFile == nullptr)
        {
            failed = true;
            return;
        }

        const pid_t result = fork();

        if (result < 0)
        {
            failed = true;
            return;
        }
        else if (result == 0)
        {
            // we're the child process...
            // make config file our stdin
            if (dup2(fileno(configFile), 0) == -1)
            {
                exit(-1);
            }

            // change working directory
            if (chdir(configFilename.getParentDirectory()
                .getFullPathName().toRawUTF8()) == -1)
            {
                exit(-1);
            }

            static const String binicaExe =
                File::getSpecialLocation(File::hostApplicationPath)
                .getParentDirectory().getChildFile("binica").getFullPathName();

            execl(binicaExe.toRawUTF8(), binicaExe.toRawUTF8(), static_cast<char*>(nullptr));
            exit(-1);
        }
        else
        {
            // we're the parent process...
            childPID = result;
        }
    }

    ~NativeICAProcess()
    {
        if (configFile != nullptr)
        {
            fclose(configFile);
        }

        // try to wait on process if we haven't yet
        if (!gotExitCode)
        {
            getExitCode();
        }
    }

    bool isRunning() const
    {
        if (failed || gotExitCode)
        {
            return false;
        }

        if (childPID == 0) // hasn't started yet nor failed
        {
            return true;
        }

        int childState;
        const int pid = waitpid(childPID, &childState, WNOHANG);
        return pid == 0 || !(WIFEXITED(childState) || WIFSIGNALED(childState));
    }

    int32 getExitCode()
    {
        if (failed)
        {
            return 1;
        }

        if (childPID == 0) // hasn't started yet nor failed
        {
            return 0;
        }

        if (gotExitCode)
        {
            return exitCode;
        }

        int childState = 0;
        const int pid = waitpid(childPID, &childState, WNOHANG);

        if (pid >= 0 && WIFEXITED(childState))
        {
            exitCode = WEXITSTATUS(childState);
            // deal with stupid negative exit code
            exitCode = exitCode >= 1 << 7 ? exitCode - 1 << 8 : exitCode;
            gotExitCode = true;
            return exitCode;
        }

        return 0;
    }

    bool failed = false;

private:
    FILE* configFile = nullptr;
    int childPID = 0;
    bool gotExitCode = false;
    int32 exitCode;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativeICAProcess);
};

ICAProcess::ICAProcess(const File& configFile)
    : nativeProcess(new NativeICAProcess(configFile))
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
