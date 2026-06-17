/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "AcuneusAutomation.h"
#include "SynthGlobals.h"

namespace
{
   constexpr int kWindowCountSliderWidth = 140;
   constexpr int kWindowCountSliderHeight = 15;
   constexpr int kAnimateCheckboxWidth = 60;
}

AcuneusAutomation::AcuneusAutomation()
{
}

AcuneusAutomation::~AcuneusAutomation()
{
}

void AcuneusAutomation::Init()
{
   ScriptModule::Init();

   if (!mLoadedAutomationScript)
   {
      std::string scriptPath = ofToResourcePath("userdata_original/scripts/acuneus_automation.py");
      if (!juce::File(scriptPath).existsAsFile())
         scriptPath = ofToDataPath("scripts/acuneus_automation.py");
      LoadScriptFile(scriptPath);
      mLoadedAutomationScript = true;
   }
}

void AcuneusAutomation::CreateUIControls()
{
   ScriptModule::CreateUIControls();

   mWindowCountSlider = new IntSlider(this, "windows", 3, static_cast<int>(mHeight) + 2, kWindowCountSliderWidth, kWindowCountSliderHeight, &mWindowCount, 1, 8);
   mAnimateCheckbox = new Checkbox(this, "animate", kWindowCountSliderWidth + 10, static_cast<int>(mHeight) + 2, &mAnimate);
   const int extraWidth = kWindowCountSliderWidth + kAnimateCheckboxWidth + 12;
   if (mWidth < extraWidth)
      mWidth = extraWidth;
   mHeight += kWindowCountSliderHeight + 4;
}

void AcuneusAutomation::DrawScriptModuleExtras()
{
   if (mWindowCountSlider)
      mWindowCountSlider->Draw();
   if (mAnimateCheckbox)
      mAnimateCheckbox->Draw();
}

void AcuneusAutomation::CheckboxUpdated(Checkbox* checkbox, double time)
{
   IDrawableModule::CheckboxUpdated(checkbox, time);

   if (checkbox != mAnimateCheckbox)
      return;

   if (mAnimate)
      RunCode(time, "start_animation()");
   else
      RunCode(time, "animation_running = False");
}

void AcuneusAutomation::ResizeScriptModuleExtras(float, float heightDelta)
{
   if (mWindowCountSlider)
      mWindowCountSlider->SetPosition(mWindowCountSlider->GetPosition(true).x, mWindowCountSlider->GetPosition(true).y + heightDelta);
   if (mAnimateCheckbox)
      mAnimateCheckbox->SetPosition(mAnimateCheckbox->GetPosition(true).x, mAnimateCheckbox->GetPosition(true).y + heightDelta);
}
