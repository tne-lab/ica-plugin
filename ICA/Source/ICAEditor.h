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

namespace ICA
{
    class ICAEditor 
        : public VisualizerEditor
        , public LabelListener
    {
    public:
        ICAEditor(GenericProcessor* parentNode);

        Visualizer* createNewCanvas() override;

        //TODO make sure to normalize the dirSuffix here, i.e. call createLegalFileName
        void labelTextChanged(Label* label) override;

        //// fixes getActiveChannels to return the right thing
        //// even if the ChannelSelector hasn't been updated yet
        //// (based on assumption that new channels are selected by default)
        //Array<int> getActiveChannels(int expectedTotalChannels);

    private:

        Component startPage;

        Label durationEditable;

        Label dirSuffixEditable;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICAEditor);
    };
}

#endif // ICA_EDITOR_H_DEFINED