#ifndef ICA_CANVAS_H_DEFINED
#define ICA_CANVAS_H_DEFINED

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

#endif // ICA_CANVAS_H_DEFINED

#include <VisualizerWindowHeaders.h>

namespace ICA
{
    class ICANode;

    class ICACanvas : public Visualizer, public Value::Listener
    {
    public:
        ICACanvas(ICANode* proc);

        void valueChanged(Value& value) override;

        void refreshState() override;
        void update() override;
        void refresh() override;
        void beginAnimation() override;
        void endAnimation() override;
        void setParameter(int, float) override;
        void setParameter(int, int, int, float) override;
            
    private:

        //enum MatrixType { mixing, unmixing };

        //class MatrixEntry : public Button
        //{
        //public:
        //    MatrixEntry(int row, int col);

        //    void paint(Graphics& g) override;

        //private:
        //    const int row;
        //    const int col;
        //    float value;

        //    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixEntry);
        //};

        //class MatrixView : public Component, public Button::Listener
        //{
        //public:
        //    MatrixView(const ICACanvas& canvas, const MatrixType type);

        //    void paint(Graphics& g) override;

        //    void buttonClicked(Button* button) override;

        //private:

        //    OwnedArray<OwnedArray<MatrixEntry>> entries;

        //    const ICACanvas& canvas;
        //    const MatrixType type;

        //    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixView);
        //};

        ICANode* const node;

        const Value& configPathVal;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICACanvas);
    };
}