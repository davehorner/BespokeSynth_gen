/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#pragma once

#include "Checkbox.h"
#include "DropdownList.h"
#include "ScriptModule.h"

class MpvPlayerAutomation : public ScriptModule, public IIntSliderListener
{
public:
   MpvPlayerAutomation();
   ~MpvPlayerAutomation() override;
   static IDrawableModule* Create() { return new MpvPlayerAutomation(); }
   static bool AcceptsAudio() { return ScriptModule::AcceptsAudio(); }
   static bool AcceptsNotes() { return ScriptModule::AcceptsNotes(); }
   static bool AcceptsPulses() { return ScriptModule::AcceptsPulses(); }

   void Init() override;
   void CreateUIControls() override;
   void IntSliderUpdated(IntSlider*, int, double) override {}
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

private:
   enum class LayoutMode
   {
      Grid = 0,
      Strip = 1,
      Cascade = 2
   };

   void DrawScriptModuleExtras() override;
   void ResizeScriptModuleExtras(float widthDelta, float heightDelta) override;

   IntSlider* mPlayerCountSlider{ nullptr };
   DropdownList* mLayoutDropdown{ nullptr };
   Checkbox* mReserveCheckbox{ nullptr };
   Checkbox* mTimelineCheckbox{ nullptr };
   int mPlayerCount{ 4 };
   int mLayout{ (int)LayoutMode::Grid };
   bool mReserveWindows{ true };
   bool mTimelineRunning{ true };
   bool mLoadedAutomationScript{ false };
};
