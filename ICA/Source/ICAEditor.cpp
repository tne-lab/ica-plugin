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
#include "ICACanvas.h"

using namespace ICA;

ICAEditor::ICAEditor(ICANode* parentNode)
    : VisualizerEditor  (parentNode, 200, false)
    , subProcLabel      ("subProcLabel", "Input subprocessor:")
    , subProcComboBox   ("subProcComboBox")
    , durationLabel     ("durationLabel", "Training duration (s):")
    , durationTextBox   ("durationTextBox", String(parentNode->getTrainDurationSec()))
    , collectedLabel    ("collectedLabel", "Collected:")
    , collectedIndicator("collectedIndicator", "")
    , dirSuffixLabel    ("dirSuffixLabel", "Output dir suffix:")
    , dirSuffixTextBox  ("dirSuffixTextBox", parentNode->getDirSuffix())
{
    // we always want to have a canvas available, makes things a lot simpler
    canvas = new ICACanvas(parentNode);

    // TODO lay out all the UI elements

    collectedIndicator.getTextValue().referTo(parentNode->getPctFullValue());
}


Visualizer* ICAEditor::createNewCanvas()
{
    return canvas;
}


void ICAEditor::labelTextChanged(Label* labelThatHasChanged)
{
    auto icaNode = static_cast<ICANode*>(getProcessor());

    if (labelThatHasChanged == &durationTextBox)
    {
        static const float minTrainSec = 1;
        static const float maxTrainSec = 2 * 60 * 60; // 2 hours (which would be ridiculous)
        float currTrainSec = icaNode->getTrainDurationSec();
        float trainSec;
        if (updateControl(labelThatHasChanged, minTrainSec, maxTrainSec, currTrainSec, trainSec))
        {
            icaNode->setTrainDurationSec(trainSec);
        }
    }
    else if (labelThatHasChanged == &dirSuffixTextBox)
    {
        String suffix = labelThatHasChanged->getText();
        if (!suffix.isEmpty())
        {
            int len = suffix.length();
            suffix = File::createLegalFileName(suffix);
            if (suffix.length() < len)
            {
                CoreServices::sendStatusMessage("Note: removing illegal characters from dir suffix");
                labelThatHasChanged->setText(suffix, dontSendNotification);
            }
        }

        icaNode->setDirSuffix(suffix);
    }
}


void ICAEditor::comboBoxChanged(ComboBox* comboBoxThatHasChanged)
{
    auto icaNode = static_cast<ICANode*>(getProcessor());

    if (comboBoxThatHasChanged == &subProcComboBox)
    {
        icaNode->setCurrSubProc(comboBoxThatHasChanged->getSelectedId());

        // update pct full indicator
        collectedIndicator.getTextValue().referTo(icaNode->getPctFullValue());
    }
}

void ICAEditor::updateSettings()
{
    // get subprocessor info from the processor and update combo box
    auto icaNode = static_cast<ICANode*>(getProcessor());

    auto& subProcInfo = icaNode->getSubProcInfo();

    subProcComboBox.clear(dontSendNotification);
    for (auto& subProcEntry : subProcInfo)
    {
        subProcComboBox.addItem(subProcEntry.second, subProcEntry.first);
    }

    uint32 currSubProc = icaNode->getCurrSubProc();
    subProcComboBox.setSelectedId(currSubProc, dontSendNotification);

    collectedIndicator.getTextValue().referTo(icaNode->getPctFullValue());
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