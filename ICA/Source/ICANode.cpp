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

ICANode::ICANode()
    : GenericProcessor  ("ICA")
    , Thread            ("ICA Thread")
    , selectionMatrix   (2, 2)
{
    setProcessorType(PROCESSOR_TYPE_FILTER);

    selectionMatrix(0, 0) = 3;
    selectionMatrix(1, 0) = 2.5;
    selectionMatrix(0, 1) = -1;
    selectionMatrix(1, 1) = selectionMatrix(1, 0) + selectionMatrix(0, 1);
    std::cout << selectionMatrix << std::endl;
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
    startThread();
    return isEnabled;
}

bool ICANode::disable()
{
    return stopThread(500);
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

void ICANode::run()
{
    String settingsFn = File::getSpecialLocation(File::hostApplicationPath)
        .getParentDirectory().getChildFile("pre_binica.sc").getFullPathName();
    
    ICAProcess proc(settingsFn);

    while (proc.isRunning())
    {
        if (threadShouldExit())
        {
            std::cout << "Thread exiting, abandoning binica run." << std::endl;
            return;
        }

        wait(100);
    }

    if (proc.failedToRun())
    {
        std::cout << "Oh no, binica failed to run!" << std::endl;
        return;
    }

    std::cout << "Received exit code " << proc.getExitCode() << " from binica." << std::endl;
}