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
#include "ScriptModule.h"

class AcuneusAutomation : public ScriptModule, public IIntSliderListener
{
public:
   AcuneusAutomation();
   ~AcuneusAutomation() override;
   static IDrawableModule* Create() { return new AcuneusAutomation(); }
   static bool AcceptsAudio() { return ScriptModule::AcceptsAudio(); }
   static bool AcceptsNotes() { return ScriptModule::AcceptsNotes(); }
   static bool AcceptsPulses() { return ScriptModule::AcceptsPulses(); }

   void Init() override;
   void CreateUIControls() override;
   void IntSliderUpdated(IntSlider*, int, double) override {}
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

private:
   void DrawScriptModuleExtras() override;
   void ResizeScriptModuleExtras(float widthDelta, float heightDelta) override;

   IntSlider* mWindowCountSlider{ nullptr };
   Checkbox* mAnimateCheckbox{ nullptr };
   int mWindowCount{ 1 };
   bool mAnimate{ false };
   bool mLoadedAutomationScript{ false };
};
