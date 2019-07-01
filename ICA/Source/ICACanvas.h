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
    class ICACanvas : public Visualizer
    {
    public:
        ICACanvas(GenericProcessor* proc);

        void refreshState() override;
        void update() override;
        void refresh() override;
        void beginAnimation() override;
        void endAnimation() override;
        void setParameter(int, float) override;
        void setParameter(int, int, int, float) override;
            
    private:
        GenericProcessor* const node;

        String displayedICADir;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ICACanvas);
    };
}