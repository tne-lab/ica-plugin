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
    " to 'ica/ICA_<timestamp>_<suffix>' within the current recordings directory.");

const String ICAEditor::resetTooltip("Reset cache; a new run will only use data"
    " from after the reset.");

ICAEditor::ICAEditor(ICANode* parentNode)
    : VisualizerEditor  (parentNode, 220, false)
    , subProcLabel      ("subProcLabel", "Input:")
    , subProcComboBox   ("subProcComboBox")
    , durationLabel     ("durationLabel", "Train for")
    , durationTextBox   ("durationTextBox", String(parentNode->getTrainDurationSec()))
    , durationUnit      ("durationUnit", "s")
    , collectedIndicator("collectedIndicator", "")
    , startButton       ("START", Font("Default", 12, Font::plain))
    , runningIndicator  ("runningIndicator", "Running...")
    , dirSuffixLabel    ("dirSuffixLabel", "Suffix:")
    , dirSuffixTextBox  ("dirSuffixTextBox", parentNode->getDirSuffix())
    , resetButton       ("RESET", Font("Default", 12, Font::plain))
    , currICAIndicator  ("currICAIndicator", "")
    , clearButton       ("X", Font("Default", 12, Font::plain))
    , configPathVal     (parentNode->addConfigPathListener(this))
    , pctFullVal        (parentNode->addPctFullListener(this))
    , icaRunningVal     (parentNode->addICARunningListener(this))
{
    tabText = "ICA";

    // we always want to have a canvas available, makes things a lot simpler
    canvas = new ICACanvas(*parentNode);

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

    durationUnit.setBounds(110, 55, 20, 20);
    durationUnit.setTooltip(durationTooltip);
    addAndMakeVisible(durationUnit);

    collectedIndicator.setBounds(0, 0, 80, 20);
    collectedIndicator.setTooltip(durationTooltip);

    startButton.setBounds(0, 0, 60, 20);
    startButton.addListener(this);

    runningIndicator.setBounds(0, 0, 70, 20);
    runningIndicator.setAlwaysOnTop(true);
    runningIndicator.setColour(Label::backgroundColourId, getBackgroundGradient().getColourAtPosition(0.5));
    runningIndicator.setOpaque(true);

    progressStartArea.setBounds(130, 55, 80, 20);
    progressStartArea.addAndMakeVisible(collectedIndicator);
    progressStartArea.addChildComponent(startButton);
    progressStartArea.addChildComponent(runningIndicator);
    addAndMakeVisible(progressStartArea);

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

    resetButton.setBounds(130, 80, 60, 20);
    resetButton.addListener(this);
    resetButton.setTooltip(resetTooltip);
    addAndMakeVisible(resetButton);
    
    currICAIndicator.setBounds(0, 0, 175, 20);

    clearButton.setBounds(175, 0, 20, 20);
    clearButton.addListener(this);
    clearButton.setVisible(!currICAIndicator.getText().isEmpty());

    currICAArea.setBounds(10, 105, 210, 20);
    currICAArea.addAndMakeVisible(currICAIndicator);
    currICAArea.addChildComponent(clearButton);
    addAndMakeVisible(currICAArea);

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
    }
}


void ICAEditor::buttonEvent(Button* button)
{
    auto icaNode = static_cast<ICANode*>(getProcessor());

    if (button == &startButton)
    {
        icaNode->startICA();
    }
    else if (button == &resetButton)
    {
        icaNode->resetCache(subProcComboBox.getSelectedId());
    }
    else if (button == &clearButton)
    {
        icaNode->resetICA(subProcComboBox.getSelectedId());
    }
    else if (button == &loadButton)
    {
        File icaBaseDir = ICANode::getICABaseDir();
        if (!icaBaseDir.isDirectory())
        {
            // default to bin/ica
            icaBaseDir = File::getSpecialLocation(File::hostApplicationPath)
                .getParentDirectory().getChildFile("ica");
        }

        FileChooser fc("Choose a binica config file...", icaBaseDir, "*.sc", true);

        if (fc.browseForFileToOpen())
        {
            File configFile = fc.getResult();
            Result loadRes = icaNode->loadICA(configFile);
            
            if (loadRes.failed())
            {
                CoreServices::sendStatusMessage("ICA load failed: " + loadRes.getErrorMessage());
            }
        }
    }
}


void ICAEditor::valueChanged(Value& value)
{
    if (value.refersToSameSourceAs(configPathVal))
    {
        String icaDir = File(value.toString()).getParentDirectory().getFileName();
        currICAIndicator.setText(icaDir, dontSendNotification);

        // "X" should be visible iff there is an ICA operation loaded for the current subproc
        clearButton.setVisible(!value.toString().isEmpty());
    }
    else if (value.refersToSameSourceAs(pctFullVal))
    {
        collectedIndicator.setText("(" + value.toString() + "% full)", dontSendNotification);
        bool full = value.getValue().equals(100);
        {
            collectedIndicator.setVisible(!full);
            startButton.setVisible(full);
        }
    }
    else if (value.refersToSameSourceAs(icaRunningVal))
    {
        bool running = value.getValue();
        runningIndicator.setVisible(running);
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
}


void ICAEditor::saveCustomParameters(XmlElement* xml)
{
    VisualizerEditor::saveCustomParameters(xml);

    xml->setAttribute("Type", "ICAEditor");

    XmlElement* stateNode = xml->createNewChildElement("STATE");
    stateNode->setAttribute("subproc", subProcComboBox.getSelectedId());
    stateNode->setAttribute("trainLength", durationTextBox.getText());
    stateNode->setAttribute("suffix", dirSuffixTextBox.getText());
}

void ICAEditor::loadCustomParameters(XmlElement* xml)
{
    VisualizerEditor::loadCustomParameters(xml);

    forEachXmlChildElementWithTagName(*xml, stateNode, "STATE")
    {
        uint32 subProc = stateNode->getIntAttribute("subproc");
        if (subProc)
        {
            subProcComboBox.setSelectedId(subProc);
        }

        durationTextBox.setText(stateNode->getStringAttribute("trainLength", durationTextBox.getText()), sendNotification);
        dirSuffixTextBox.setText(stateNode->getStringAttribute("suffix", dirSuffixTextBox.getText()), sendNotification);
    }
}
