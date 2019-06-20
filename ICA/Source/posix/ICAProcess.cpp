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
    NativeICAProcess(const String& settingsPath)
    {
        // open settings file
        settingsFile = fopen(settingsPath.toRawUTF8(), "r");
        if (settingsFile == nullptr)
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
            // make settings file our stdin
            if (dup2(fileno(settingsFile), 0) == -1)
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
        if (settingsFile != nullptr)
        {
            fclose(settingsFile);
        }
    }

    bool isRunning() const
    {
        if (failed)
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

    uint32 getExitCode() const
    {
        if (failed)
        {
            return 1;
        }

        if (childPID == 0) // hasn't started yet nor failed
        {
            return 0;
        }

        int childState = 0;
        const int pid = waitpid(childPID, &childState, WNOHANG);

        if (pid >= 0 && WIFEXITED(childState))
        {
            return WEXITSTATUS(childState);
        }

        return 0;
    }

    bool failed = false;

private:
    FILE* settingsFile = nullptr;
    int childPID = 0;

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
