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
#include <EditorHeaders.h> // for electrode button

#include "ICANode.h"

namespace ICA
{
    class ICACanvas : public Visualizer, public Value::Listener
    {
    public:
        ICACanvas(ICANode& proc);

        void resized() override;
        void paint(Graphics& g) override;

        void valueChanged(Value& value) override;

        void refreshState() override;
        void update() override;
        void refresh() override;
        void beginAnimation() override;
        void endAnimation() override;
        void setParameter(int, float) override;
        void setParameter(int, int, int, float) override;
            
    private:

        class ColourBar : public Component
        {
        public:
            explicit ColourBar(float max = 0.0f);

            // maximum positive and minimum negative value
            void resetRange(float max = 0.0f);

            // sets absMax to abs(val) if it is currently smalller.
            void ensureValueInRange(float val);

            float getAbsoluteMax() const;

            void paint(Graphics& g) override;

        private:
            float absMax;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColourBar);
        };

        class MatrixView : public Component
        {
        public:
            MatrixView(ColourBar& bar);

            // changes the underlying data and automatically repaints.
            void setData(const Matrix& newData);

            void paint(Graphics& g) override;

        private:
            ColourBar& colourBar;

            Matrix data;
            //const MatrixType type;

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixView);
        };

        ICANode& node;

        const Value& configPathVal;

        Viewport viewport;

        // hierarchical singleton struct of displayed components
        // it seemed messy to have all the nested components as top-level members of ICACanvas
        // hopefully this isn't even more confusing...
        struct ContentCanvas : public Component
        {
            ContentCanvas();

            void update(const ICAOperation& op);

            struct MixingInfo : public Component
            {
                MixingInfo();

                void update(int startX, const Matrix& mixing);

                ColourBar colourBar;
                Label title;
                OwnedArray<Label> chanLabels;
                MatrixView matrixView;
                MatrixView normView;
                Label normLabel;

            } mixingInfo;

            Label multiplySign1;

            struct ComponentSelectionArea : public Component
            {
                ComponentSelectionArea();

                void update(int startX, const ICAOperation& op);

                Label title;
                DrawableRectangle selectionGrid;
                OwnedArray<ElectrodeButton> componentButtons;

            } componentSelectionArea;

            Label multiplySign2;

            struct UnmixingInfo : public Component
            {
                UnmixingInfo();

                void update(int startX, const Matrix& unmixing);

                Label title;
                MatrixView matrixView;
                OwnedArray<Label> chanLabels;
                ColourBar colourBar;

            } unmixingInfo;

        } canvas;

        // colorbrewer red/blue map
        static const ColourGradient colourMap;

        static const int colourBarY;
        static const int colourBarHeight;

        // width of colourBar and side length of each matrix entry
        static const int unitLength;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICACanvas);
    };
}