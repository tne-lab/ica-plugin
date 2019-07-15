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

#include "ICACanvas.h"

using namespace ICA;


// confusing behavior: the gradient starts at this y position in any component that
// calls g.setGradientFill (regardless of what area is actually filled).
const int ICACanvas::colourBarY = 20;
const int ICACanvas::colourBarHeight = 150;

const int ICACanvas::unitLength = 20;

ICACanvas::ICACanvas(ICANode& proc)
    : node              (proc)
    , configPathVal     (proc.addConfigPathListener(this))
    , canvas            (*this)
{
    viewport.setViewedComponent(&canvas, false);
    viewport.setScrollBarsShown(true, true);
    addChildComponent(viewport);
}

void ICACanvas::resized()
{
    int vpWidth = getWidth();
    int vpHeight = getHeight();
    viewport.setSize(vpWidth, vpHeight);
}

void ICACanvas::paint(Graphics& g)
{
    g.fillAll(Colours::grey);
}

void ICACanvas::valueChanged(Value& value)
{
    if (value.refersToSameSourceAs(configPathVal))
    {
        // this is how we get notified of a change to the ICA operation.
        // update everything.
        update();
    }    
}

void ICACanvas::buttonClicked(Button* button)
{
    // must be an electrode button
    auto eButton = static_cast<ElectrodeButton*>(button);
    int kComp = eButton->getChannelNum() - 1; // to 0-based
    bool selected = eButton->getToggleState();

    ScopedPointer<ScopedWriteLock> icaLock;
    ICAOperation* op = node.writeICAOperation(icaLock);

    if (!op || op->enabledChannels.size() <= kComp)
    {
        // uh-oh, the canvas is out of sync. the valueChanged callback should resolve this.
        return;
    }

    if (selected)
    {
        op->rejectedComponents.removeValue(kComp);
    }
    else
    {
        op->rejectedComponents.add(kComp);
    }
}

void ICACanvas::update()
{
    ScopedPointer<ScopedReadLock> icaLock;
    const ICAOperation* op = node.readICAOperation(icaLock);

    if (!op)
    {
        viewport.setVisible(false);
        return;
    }

    const StringArray* chanNames = node.getCurrSubProcChannelNames();

    if (!chanNames)
    {
        viewport.setVisible(false);
        return;
    }

    canvas.update({ *op, *chanNames });
    viewport.setVisible(true);
}

// not using animation features
void ICACanvas::refreshState() {}
void ICACanvas::refresh() {}
void ICACanvas::beginAnimation() {}
void ICACanvas::endAnimation() {}
void ICACanvas::setParameter(int, float) {}
void ICACanvas::setParameter(int, int, int, float) {}


ICACanvas::MatrixView::MatrixView(ColourBar& bar)
    : Component("Matrix view")
    , colourBar(bar)
{}

void ICACanvas::MatrixView::setData(const Matrix& newData)
{
    data = newData;
    float absMaxVal = data.lpNorm<Eigen::Infinity>();
    colourBar.ensureValueInRange(absMaxVal);
    repaint();
}

void ICACanvas::MatrixView::paint(Graphics& g)
{
    int nRows = data.rows();
    int nCols = data.cols();

    float rowHeight = float(getHeight()) / nRows;
    float colWidth = float(getWidth()) / nCols;

    for (int r = 0; r < nRows; ++r)
    {
        for (int c = 0; c < nCols; ++c)
        {
            float limit = colourBar.absMax;
            float mappedPos = jmap(data(r, c), -limit, limit, 0.0f, 1.0f);
            g.setColour(colourBar.colourMap.getColourAtPosition(mappedPos));
            g.fillRect(c * colWidth, r * rowHeight, colWidth, rowHeight);
        }
    }
}


ICACanvas::ColourBar::ColourBar(float max)
    : Component ("Colour bar")
    , colourMap({ 0x21, 0x66, 0xac }, 0, colourBarY + colourBarHeight,
                { 0xb2, 0x18, 0x2b }, 0, colourBarY, false)
{
    colourMap.addColour(0.5, Colours::white);

    setSize(50, colourBarHeight + 2 * colourBarY);
    resetRange(max);
}

void ICACanvas::ColourBar::resetRange(float max)
{
    absMax = max < 0 ? -max : max;
    repaint();
}

void ICACanvas::ColourBar::ensureValueInRange(float val)
{
    if (val < -absMax || val > absMax)
    {
        resetRange(val);
    }
}

void ICACanvas::ColourBar::paint(Graphics& g)
{
    g.setColour(Colours::white);
    g.drawSingleLineText(String(absMax, 3), 0, colourBarY - 5);
    g.drawSingleLineText(String(-absMax, 3), 0, getHeight());

    g.setGradientFill(colourMap);
    g.fillRect(0, colourBarY, unitLength, colourBarHeight);
}


ICACanvas::ContentCanvas::ContentCanvas(ICACanvas& visualizer)
    : Component             ("ICA canvas")
    , multiplySign1         ("X 1", L"\u00d7")
    , componentSelectionArea(visualizer)
    , multiplySign2         ("X 2", L"\u00d7")
    , visualizer            (visualizer)
{
    mixingInfo.setTopLeftPosition(30, 30);
    addAndMakeVisible(mixingInfo);

    formatLargeLabel(multiplySign1, 48);
    addAndMakeVisible(multiplySign1);

    addAndMakeVisible(componentSelectionArea);

    formatLargeLabel(multiplySign2, 48);
    addAndMakeVisible(multiplySign2);

    addAndMakeVisible(unmixingInfo);
}

void ICACanvas::ContentCanvas::update(UpdateInfo info)
{
    mixingInfo.update(info);

    multiplySign1.setSize(multiplySign1.getWidth(), mixingInfo.matrixView.getHeight());
    multiplySign1.setTopLeftPosition(getLocalPoint(&mixingInfo,
        mixingInfo.matrixView.getBounds().getTopRight()));

    componentSelectionArea.setTopLeftPosition(
        mixingInfo.getBounds().getTopRight().translated(multiplySign1.getWidth(), 0));
    componentSelectionArea.update(info);

    multiplySign2.setSize(multiplySign2.getWidth(), componentSelectionArea.background.getHeight());
    multiplySign2.setTopLeftPosition(getLocalPoint(&componentSelectionArea,
        componentSelectionArea.background.getBounds().getTopRight()));

    unmixingInfo.setTopLeftPosition(
        componentSelectionArea.getBounds().getTopRight().translated(multiplySign2.getWidth(), 0));
    unmixingInfo.update(info);

    // make sure everything fits
    setSize(unmixingInfo.getRight(), mixingInfo.getBottom());
}


void ICACanvas::ContentCanvas::formatLargeLabel(Label& label, int width)
{
    label.setFont(getLargeFont());
    label.setColour(Label::textColourId, Colours::white);
    label.setSize(width, 30);
    label.setJustificationType(Justification::centred);
}

int ICACanvas::ContentCanvas::getNaturalWidth(const Label& label)
{
    return label.getFont().getStringWidth(label.getText());
}

Font ICACanvas::ContentCanvas::getLargeFont()
{
    return{ 24, Font::bold };
}

Font ICACanvas::ContentCanvas::getSmallFont()
{
    return{ "Default", 12, Font::plain };
}

ICACanvas::ContentCanvas::MixingInfo::MixingInfo()
    : title     ("Mixing title", "MIXING")
    , matrixView(matrixColourBar)
    , normView  (normColourBar)
    , normLabel ("Mixing norm label", "NORM")
{
    formatLargeLabel(title);
    addAndMakeVisible(title);

    matrixColourBar.setTopLeftPosition(0, title.getHeight() - colourBarY);
    addAndMakeVisible(matrixColourBar);

    addAndMakeVisible(matrixView);

    addAndMakeVisible(normColourBar);

    addAndMakeVisible(normView);

    formatLargeLabel(normLabel);
    addAndMakeVisible(normLabel);
}

void ICACanvas::ContentCanvas::MixingInfo::update(UpdateInfo info)
{
    matrixColourBar.resetRange();
    normColourBar.resetRange();

    int nChans = info.op.mixing.rows();
    int nComps = info.op.mixing.cols();

    matrixView.setSize(nComps * unitLength, nChans * unitLength);
    matrixView.setData(info.op.mixing);

    normView.setSize(nComps * unitLength, unitLength);
    normView.setData(info.op.mixing.colwise().norm());

    // layout
    StringArray usedChannelNames;
    Font chanLabelFont(12, Font::plain);
    int labelWidth = 0;

    for (int c = 0; c < nChans; ++c)
    {
        const String& chanName = info.chanNames[info.op.enabledChannels[c]];
        usedChannelNames.add(chanName);
        labelWidth = jmax(labelWidth, chanLabelFont.getStringWidth(chanName));
    }

    // add some extra buffer
    labelWidth += 10;

    if (chanLabels.size() > nChans)
    {
        chanLabels.removeLast(chanLabels.size() - nChans);
    }

    for (int c = 0; c < nChans; ++c)
    {
        Label* label = chanLabels[c];
        if (!label)
        {
            label = chanLabels.set(c, new Label());
            label->setColour(Label::textColourId, Colours::white);
            label->setFont(chanLabelFont);
            label->setJustificationType(Justification::right);
            label->setTopLeftPosition(matrixColourBar.getRight() + unitLength, title.getHeight() + c * unitLength);
            addAndMakeVisible(label);
        }

        label->setText(usedChannelNames[c], dontSendNotification);
        label->setSize(labelWidth, unitLength);
    }

    title.setSize(jmax(matrixView.getWidth(), getNaturalWidth(title)), title.getHeight());
    title.setTopLeftPosition(chanLabels[0]->getRight(), 0);

    matrixView.setTopLeftPosition(title.getX(), title.getBottom());

    normView.setTopLeftPosition(title.getX(), jmax(matrixView.getBottom(), matrixColourBar.getBottom()) + 4 + colourBarY);

    normColourBar.setTopLeftPosition(0, normView.getY() - colourBarY);

    normLabel.setSize(jmax(matrixView.getWidth(), getNaturalWidth(normLabel)), normLabel.getHeight());
    normLabel.setTopLeftPosition(title.getX(), normView.getBottom());

    setSize(jmax(title.getRight(), matrixView.getRight()), jmax(normColourBar.getBottom(), normLabel.getBottom()));
}

ICACanvas::ContentCanvas::ComponentSelectionArea::ComponentSelectionArea(ICACanvas& visualizer)
    : title         ("Component selection title", "KEEP COMPONENTS:")
    , background    ("Component selection background", "")
    , visualizer    (visualizer)
    , buttonFont    (getSmallFont())
    , allButton     ("ALL", buttonFont)
    , noneButton    ("NONE", buttonFont)
    , invertButton  ("INVERT", buttonFont)
{
    formatLargeLabel(title);
    addAndMakeVisible(title);

    background.setColour(Label::backgroundColourId, Colours::lightgrey);
    background.setTopLeftPosition(0, title.getHeight());
    addAndMakeVisible(background);

    allButton.addListener(this);
    addAndMakeVisible(allButton);

    noneButton.addListener(this);
    addAndMakeVisible(noneButton);

    invertButton.addListener(this);
    addAndMakeVisible(invertButton);
}

void ICACanvas::ContentCanvas::ComponentSelectionArea::update(UpdateInfo info)
{
    int nComps = info.op.mixing.cols();

    background.setSize(nComps * unitLength, nComps * unitLength);

    title.setSize(jmax(background.getWidth(), getNaturalWidth(title)), title.getHeight());

    // component buttons
    if (componentButtons.size() > nComps)
    {
        componentButtons.removeLast(componentButtons.size() - nComps);
    }

    for (int c = 0; c < nComps; ++c)
    {
        ElectrodeButton* btn = componentButtons[c];
        if (!btn)
        {
            btn = componentButtons.set(c, new ElectrodeButton(c + 1));
            btn->addListener(&visualizer);
            btn->setAlwaysOnTop(true);
            btn->setSize(unitLength, unitLength);
            btn->setTopLeftPosition(background.getPosition().translated(unitLength * c, unitLength * c));

            addAndMakeVisible(btn);
        }

        btn->setToggleState(true, dontSendNotification);
    }

    for (int cOff : info.op.rejectedComponents)
    {
        componentButtons[cOff]->setToggleState(false, dontSendNotification);
    }

    // UtilityButtons, to control component buttons
    allButton.setTopLeftPosition(background.getBounds().getBottomLeft().translated(0, 3));

    int buttonWidth = buttonFont.getStringWidth(invertButton.getButtonText());

    if (background.getWidth() / 3 >= buttonWidth)
    {
        // display buttons side-by-side
        buttonWidth = background.getWidth() / 3 - 2;

        noneButton.setTopLeftPosition(allButton.getPosition().translated(buttonWidth + 3, 0));
        invertButton.setTopLeftPosition(noneButton.getPosition().translated(buttonWidth + 3, 0));
    }
    else
    {
        // display buttons stacked
        buttonWidth = jmax(background.getWidth(), buttonWidth);

        noneButton.setTopLeftPosition(allButton.getPosition().translated(0, unitLength + 2));
        invertButton.setTopLeftPosition(noneButton.getPosition().translated(0, unitLength + 2));
    }

    allButton.setSize(buttonWidth, unitLength);
    noneButton.setSize(buttonWidth, unitLength);
    invertButton.setSize(buttonWidth, unitLength);

    setSize(jmax(title.getRight(), background.getRight(), invertButton.getRight()), invertButton.getBottom());
}

void ICACanvas::ContentCanvas::ComponentSelectionArea::buttonClicked(Button* button)
{
    if (button == &allButton)
    {
        for (Button* btn : componentButtons)
        {
            btn->setToggleState(true, sendNotification);
        }
    }
    else if (button == &noneButton)
    {
        for (Button* btn : componentButtons)
        {
            btn->setToggleState(false, sendNotification);
        }
    }
    else if (button == &invertButton)
    {
        for (Button* btn : componentButtons)
        {
            btn->setToggleState(!btn->getToggleState(), sendNotification);
        }
    }
}


ICACanvas::ContentCanvas::UnmixingInfo::UnmixingInfo()
    : title     ("Unmixing title", "UNMIXING")
    , matrixView(colourBar)
{
    formatLargeLabel(title);
    title.setTopLeftPosition(0, 0);
    addAndMakeVisible(title);

    matrixView.setTopLeftPosition(0, title.getBottom());
    addAndMakeVisible(matrixView);

    addAndMakeVisible(colourBar);
}

void ICACanvas::ContentCanvas::UnmixingInfo::update(UpdateInfo info)
{
    colourBar.resetRange();

    int nComps = info.op.unmixing.rows();
    int nChans = info.op.unmixing.cols();

    matrixView.setSize(nChans * unitLength, nComps * unitLength);
    matrixView.setData(info.op.unmixing);

    // layout
    title.setSize(jmax(matrixView.getWidth(), getNaturalWidth(title)), title.getHeight());

    colourBar.setTopLeftPosition(title.getBounds().getBottomRight().translated(unitLength * 3 / 2, -colourBarY));
    
    if (chanLabels.size() > nChans)
    {
        chanLabels.removeLast(chanLabels.size() - nChans);
    }

    int labelHeight = 0;
    for (int c = 0; c < nChans; ++c)
    {
        Label* label = chanLabels[c];
        if (!label)
        {
            label = chanLabels.set(c, new Label());
            label->setColour(Label::textColourId, Colours::white);
            label->setFont({ 12, Font::plain });
            label->setJustificationType(Justification::left);

            int x = matrixView.getX() + (c + 1) * unitLength; // pivot point is right side of column
            int y = matrixView.getBottom();
            label->setTopLeftPosition(x, y);
            // rotate 90 degrees 
            label->setTransform(AffineTransform::rotation(float_Pi / 2, x, y));

            addAndMakeVisible(label);
        }

        label->setText(info.chanNames[info.op.enabledChannels[c]], dontSendNotification);
        label->setSize(getNaturalWidth(*label) + 10, unitLength);

        labelHeight = jmax(labelHeight, label->getBoundsInParent().getHeight());
    }

    setSize(colourBar.getRight(), jmax(colourBar.getBottom(), matrixView.getBottom() + labelHeight));
}