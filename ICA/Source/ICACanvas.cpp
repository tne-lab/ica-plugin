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
#include "ICANode.h"

using namespace ICA;

ICACanvas::ICACanvas(ICANode* proc)
    : node          (proc)
    , configPathVal (proc->addConfigPathListener(this))
{}

void ICACanvas::valueChanged(Value& value)
{
    if (value.refersToSameSourceAs(configPathVal))
    {
        // get new matrices and update MatrixViews
    }    
}

void ICACanvas::refreshState() {}

void ICACanvas::update() {}

void ICACanvas::refresh() {}

void ICACanvas::beginAnimation() {}

void ICACanvas::endAnimation() {}

void ICACanvas::setParameter(int, float) {}

void ICACanvas::setParameter(int, int, int, float) {}