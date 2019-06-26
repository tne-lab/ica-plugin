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

#include "ICAEditor.h"
#include "ICACanvas.h"

using namespace ICA;

ICAEditor::ICAEditor(GenericProcessor* parentNode)
    : VisualizerEditor(parentNode, 200, false)
{}


Visualizer* ICAEditor::createNewCanvas()
{
    canvas = new ICACanvas(getProcessor());
    return canvas;
}


//
//Array<int> ICAEditor::getActiveChannels(int expectedTotalChannels)
//{
//    int numChannelButtons = channelSelector->getNumChannels();
//
//    Array<int> activeChans = GenericEditor::getActiveChannels();
//
//    if (expectedTotalChannels < numChannelButtons)
//    {
//        // remove any that are too large
//        int nActiveChans = activeChans.size();
//        for (int i = 0, *it = activeChans.begin(); i < nActiveChans; ++i)
//        {
//            if (*it >= expectedTotalChannels)
//            {
//                activeChans.remove(it);
//            }
//            else
//            {
//                ++it;
//            }
//        }
//    }
//
//    // insert new channels at end
//    for (int newChan = numChannelButtons; newChan < expectedTotalChannels; ++newChan)
//    {
//        activeChans.add(newChan);
//    }
//
//    return activeChans;
//}