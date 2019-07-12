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

#include <tuple>

#include "ICANode.h"

namespace ICA
{
    class ICACanvas : public Visualizer, public Value::Listener, public Button::Listener
    {
    public:
        ICACanvas(ICANode& proc);

        void resized() override;
        void paint(Graphics& g) override;

        void valueChanged(Value& value) override;

        void buttonClicked(Button*) override;

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

            // colorbrewer red/blue map
            ColourGradient colourMap;

            float absMax;

        private:
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

        struct UpdateInfo
        {
            const ICAOperation& op;
            const StringArray& chanNames;
        };

        // hierarchical singleton struct of displayed components
        // it seemed messy to have all the nested components as top-level members of ICACanvas
        // hopefully this isn't even more confusing...
        struct ContentCanvas : public Component
        {
            ContentCanvas(ICACanvas& visualizer);
            
            void update(UpdateInfo info);

            struct MixingInfo : public Component
            {
                MixingInfo();

                void update(UpdateInfo info);

                ColourBar matrixColourBar;
                Label title;
                OwnedArray<Label> chanLabels;
                MatrixView matrixView;

                ColourBar normColourBar;
                MatrixView normView;
                Label normLabel;

            } mixingInfo;

            Label multiplySign1;

            struct ComponentSelectionArea : public Component, public Button::Listener
            {
                ComponentSelectionArea(ICACanvas& visualizer);

                void update(UpdateInfo info);

                // only for the UtilityButtons
                void buttonClicked(Button* button) override;

                Label title;
                Label background;
                OwnedArray<ElectrodeButton> componentButtons;

                Font buttonFont;

                UtilityButton allButton;
                UtilityButton noneButton;
                UtilityButton invertButton;

            private:
                ICACanvas& visualizer;

            } componentSelectionArea;

            Label multiplySign2;

            struct UnmixingInfo : public Component
            {
                UnmixingInfo();

                void update(UpdateInfo info);

                Label title;
                MatrixView matrixView;
                OwnedArray<Label> chanLabels;
                ColourBar colourBar;

            } unmixingInfo;

        private:

            static void formatLargeLabel(Label& label, int width = 0);

            static int getNaturalWidth(const Label& label);

            static Font getLargeFont();
            static Font getSmallFont();

            ICACanvas& visualizer;

        } canvas;

        static const int colourBarY;
        static const int colourBarHeight;

        // width of colourBar and side length of each matrix entry
        static const int unitLength;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICACanvas);
    };
}