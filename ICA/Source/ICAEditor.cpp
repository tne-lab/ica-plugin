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

const String ICAEditor::subProcTooltip("An ICA operation can be computed and stored"
    " for each input subprocessor. The input selected here is the one that a newly"
    " calculated or loaded operation will be applied to, and also the one that is"
    " displayed in the visualizer tab. You can select which channels the ICA operation"
    " should apply to (down to a minimum of 2) in the 'PARAMS' tab in the drawer.");

const String ICAEditor::durationTooltip("At least 2 minutes of training is"
    " recommended for best results. After the buffer fills with training data,"
    " it will continue to stay updated with new samples while discarding old samples.");

const String ICAEditor::dirSuffixTooltip("Output of the ICA run will be saved"
    " to a directory 'ICA_<timestamp>_<suffix>' within either the Open Ephys"
    " executable folder or, if a recording is active, the recording folder.");

ICAEditor::ICAEditor(ICANode* parentNode)
    : VisualizerEditor  (parentNode, 220, false)
    , subProcLabel      ("subProcLabel", "Input:")
    , subProcComboBox   ("subProcComboBox")
    , durationLabel     ("durationLabel", "Train for")
    , durationTextBox   ("durationTextBox", String(parentNode->getTrainDurationSec()))
    , collectedLabel    ("collectedLabel", "s   (")
    , collectedIndicator("collectedIndicator", "")
    , dirSuffixLabel    ("dirSuffixLabel", "Suffix:")
    , dirSuffixTextBox  ("dirSuffixTextBox", parentNode->getDirSuffix())
    , startButton       ("START", Font("Default", 12, Font::plain))
    , currICAIndicator  ("currICAIndicator", "")
    , clearButton       ("X", Font("Default", 12, Font::plain))
{
    // we always want to have a canvas available, makes things a lot simpler
    canvas = new ICACanvas(parentNode);

    subProcLabel.setBounds(10, 30, 50, 20);
    subProcLabel.setTooltip(subProcTooltip);
    addAndMakeVisible(subProcLabel);

    subProcComboBox.setBounds(60, 30, 130, 22);
    subProcComboBox.addListener(this);
    subProcComboBox.setTooltip(subProcTooltip);
    addAndMakeVisible(subProcComboBox);

    durationLabel.setBounds(10, 55, 60, 20);
    durationLabel.setTooltip(durationTooltip);
    addAndMakeVisible(durationLabel);

    durationTextBox.setBounds(70, 55, 40, 20);
    durationTextBox.setEditable(true);
    durationTextBox.addListener(this);
    durationTextBox.setColour(Label::backgroundColourId, Colours::grey);
    durationTextBox.setColour(Label::textColourId, Colours::white);
    durationTextBox.setTooltip(durationTooltip);
    addAndMakeVisible(durationTextBox);

    collectedLabel.setBounds(110, 55, 30, 20);
    collectedLabel.setTooltip(durationTooltip);
    addAndMakeVisible(collectedLabel);

    collectedIndicator.setBounds(130, 55, 80, 20);
    collectedIndicator.getTextValue().referTo(parentNode->getPctFullValue());
    collectedIndicator.setTooltip(durationTooltip);
    addAndMakeVisible(collectedIndicator);

    dirSuffixLabel.setBounds(10, 80, 50, 20);
    dirSuffixLabel.setTooltip(dirSuffixTooltip);
    addAndMakeVisible(dirSuffixLabel);

    dirSuffixTextBox.setBounds(65, 80, 50, 20);
    dirSuffixTextBox.setEditable(true);
    dirSuffixTextBox.addListener(this);
    dirSuffixTextBox.setColour(Label::backgroundColourId, Colours::grey);
    dirSuffixTextBox.setColour(Label::textColourId, Colours::white);
    dirSuffixTextBox.setTooltip(dirSuffixTooltip);
    addAndMakeVisible(dirSuffixTextBox);

    startButton.setBounds(130, 80, 50, 20);
    startButton.addListener(this);
    startButton.setEnabled(CoreServices::getAcquisitionStatus());
    addAndMakeVisible(startButton);
    
    currICAIndicator.setBounds(10, 105, 175, 20);
    currICAIndicator.getTextValue().referTo(parentNode->getICAOutputDirValue());
    currICAIndicator.getTextValue().addListener(this);
    addAndMakeVisible(currICAIndicator);

    clearButton.setBounds(185, 105, 20, 20);
    clearButton.addListener(this);
    clearButton.setVisible(!currICAIndicator.getText().isEmpty());
    addChildComponent(clearButton);

    loadButton.addListener(this);
    loadButton.setBounds(desiredWidth - 70, 5, 15, 15);
    addAndMakeVisible(loadButton);
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

        // update indicators
        collectedIndicator.getTextValue().referTo(icaNode->getPctFullValue());
        currICAIndicator.getTextValue().referTo(icaNode->getICAOutputDirValue());
    }
}


void ICAEditor::buttonEvent(Button* button)
{
    auto icaNode = static_cast<ICANode*>(getProcessor());

    if (button == &startButton)
    {
        icaNode->startICA();
    }
    else if (button == &clearButton)
    {
        icaNode->resetICA(subProcComboBox.getSelectedId());
    }
    else if (button == &loadButton)
    {
        File icaBaseDir = ICANode::getICABaseDir();
        FileChooser fc("Choose a binica config file...", icaBaseDir, "*.sc", true);

        if (fc.browseForFileToOpen())
        {
            File configFile = fc.getResult();
            icaNode->loadICA(configFile);
        }
    }
}


void ICAEditor::valueChanged(Value& value)
{
    if (value.refersToSameSourceAs(currICAIndicator.getTextValue()))
    {
        // "X" should be visible iff there is an ICA operation loaded for the current subproc
        clearButton.setVisible(!value.toString().isEmpty());
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
    currICAIndicator.getTextValue().referTo(icaNode->getICAOutputDirValue());
}


void ICAEditor::startAcquisition()
{
    startButton.setEnabled(true);
}

void ICAEditor::stopAcquisition()
{
    startButton.setEnabled(false);
}