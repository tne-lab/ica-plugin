#ifndef ICA_EDITOR_H_DEFINED
#define ICA_EDITOR_H_DEFINED

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

#include <VisualizerEditorHeaders.h>

#include <sstream> // string parsing

namespace ICA
{
    class ICANode;

    class ICAEditor 
        : public VisualizerEditor
        , public Label::Listener
        , public ComboBox::Listener
        , public Value::Listener
    {
    public:
        ICAEditor(ICANode* parentNode);

        Visualizer* createNewCanvas() override;

        //TODO make sure to normalize the dirSuffix here, i.e. call createLegalFileName
        void labelTextChanged(Label* labelThatHasChanged) override;

        void comboBoxChanged(ComboBox* comboBoxThatHasChanged) override;

        void buttonEvent(Button* button) override;

        void valueChanged(Value& value) override;

        void updateSettings() override;

    private:

        // (utility functions copied from PhaseCalculator)
        /*
        * Tries to read a number of type out from input. Returns false if unsuccessful.
        * Otherwise, returns true and writes the result to *out.
        */
        template<typename T>
        static bool readNumber(const String& input, T& out)
        {
            std::istringstream istream(input.toStdString());
            istream >> out;
            return !istream.fail();
        }

        /*
        * Return whether the control contained a valid input between min and max, inclusive.
        * If so, it is stored in *out and the control is updated with the parsed input.
        * Otherwise, the control is reset to defaultValue.
        *
        * In header to make sure specializations not used in ICAEditor.cpp
        * are still available to other translation units.
        */
        template<typename Ctrl, typename T>
        static bool updateControl(Ctrl* c, const T min, const T max,
            const T defaultValue, T& out)
        {
            if (readNumber(c->getText(), out))
            {
                out = jmax(min, jmin(max, out));
                c->setText(String(out), dontSendNotification);
                return true;
            }

            c->setText(String(defaultValue), dontSendNotification);
            return false;
        }

        Component startPage;

        Label subProcLabel;
        ComboBox subProcComboBox;

        Label durationLabel;
        Label durationTextBox;

        Label collectedLabel;
        Label collectedIndicator;

        Label dirSuffixLabel;
        Label dirSuffixTextBox; 

        UtilityButton startButton;

        Label currICAIndicator;
        UtilityButton clearButton;

        LoadButton loadButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICAEditor);
    };
}

#endif // ICA_EDITOR_H_DEFINED