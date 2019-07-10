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

const ColourGradient ICACanvas::colourMap = []
{
    static const Colour negColour(0x21, 0x66, 0xac);
    static const Colour posColour(0xb2, 0x18, 0x2b);

    static ColourGradient map(negColour, 0, 220, posColour, 0, 20, false);
    map.addColour(0.5, Colours::white);
    return map;
}();

// confusing behavior: the gradient starts at this y position in any component that
// calls g.setGradientFill (regardless of what area is actually filled).
const int ICACanvas::colourBarY = colourMap.point2.y;
const int ICACanvas::colourBarHeight = colourMap.point1.y - colourBarY;

const int ICACanvas::unitLength = 20;

ICACanvas::ICACanvas(ICANode& proc)
    : node              (proc)
    , configPathVal     (proc.addConfigPathListener(this))
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
    canvas.setSize(vpWidth, vpHeight); // for now
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

        ScopedPointer<ScopedReadLock> icaLock;
        const ICAOperation* op = node.getICAOperation(icaLock);

        if (op)
        {
            canvas.update(*op);
            viewport.setVisible(true);
        }
        else
        {
            viewport.setVisible(false);
        }
    }    
}

void ICACanvas::refreshState() {}

void ICACanvas::update() {}

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
            float limit = colourBar.getAbsoluteMax();
            float mappedPos = jmap(data(r, c), -limit, limit, 0.0f, 1.0f);
            g.setColour(colourMap.getColourAtPosition(mappedPos));
            g.fillRect(c * colWidth, r * rowHeight, colWidth, rowHeight);
        }
    }
}


ICACanvas::ColourBar::ColourBar(float max)
    : Component ("Colour bar")
{
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

float ICACanvas::ColourBar::getAbsoluteMax() const
{
    return absMax;
}

void ICACanvas::ColourBar::paint(Graphics& g)
{
    g.setColour(Colours::white);
    g.drawSingleLineText(String(absMax), 0, colourBarY - 5);
    g.drawSingleLineText(String(-absMax), 0, getHeight());

    g.setGradientFill(colourMap);
    g.fillRect(0, colourBarY, unitLength, colourBarHeight);
}


ICACanvas::ContentCanvas::ContentCanvas()
    : Component("ICA canvas")
{
    for (Label* xSign : { &multiplySign1, &multiplySign2 })
    {
        xSign->setText("\u00d7", dontSendNotification);
        xSign->setSize(20, 20);
        xSign->setFont({ 16, Font::bold });
        xSign->setJustificationType(Justification::centred);
        xSign->setColour(Label::textColourId, Colours::white);
        addAndMakeVisible(xSign);
    }

    addAndMakeVisible(mixingInfo);
    addAndMakeVisible(componentSelectionArea);
    addAndMakeVisible(unmixingInfo);
}

void ICACanvas::ContentCanvas::update(const ICAOperation& op)
{
    mixingInfo.update(10, op.mixing);
    // y-coordinate of center line
    int centreY = mixingInfo.matrixView.getY() + mixingInfo.matrixView.getHeight() / 2;

    multiplySign1.setCentrePosition(mixingInfo.getRight() + multiplySign1.getWidth() / 2, centreY);
    
    componentSelectionArea.update(multiplySign1.getRight(), op);

    multiplySign2.setCentrePosition(componentSelectionArea.getRight() + multiplySign2.getWidth() / 2, centreY);

    unmixingInfo.update(multiplySign2.getRight(), op.unmixing);
}


ICACanvas::ContentCanvas::MixingInfo::MixingInfo()
    : matrixView(colourBar)
    , normView  (colourBar)
{}

void ICACanvas::ContentCanvas::MixingInfo::update(int startX, const Matrix& mixing)
{

}

ICACanvas::ContentCanvas::ComponentSelectionArea::ComponentSelectionArea()
{}

void ICACanvas::ContentCanvas::ComponentSelectionArea::update(int startX, const ICAOperation& op)
{}

ICACanvas::ContentCanvas::UnmixingInfo::UnmixingInfo()
    : matrixView(colourBar)
{}

void ICACanvas::ContentCanvas::UnmixingInfo::update(int startX, const Matrix& unmixing)
{}