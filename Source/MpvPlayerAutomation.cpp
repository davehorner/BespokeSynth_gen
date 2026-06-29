/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "MpvPlayerAutomation.h"
#include "SynthGlobals.h"

namespace
{
   constexpr int kPlayerCountSliderWidth = 110;
   constexpr int kControlHeight = 15;
   constexpr int kLayoutDropdownWidth = 82;
   constexpr int kReserveCheckboxWidth = 70;
   constexpr int kTimelineCheckboxWidth = 72;
}

MpvPlayerAutomation::MpvPlayerAutomation()
{
}

MpvPlayerAutomation::~MpvPlayerAutomation()
{
}

void MpvPlayerAutomation::Init()
{
   ScriptModule::Init();

   if (!mLoadedAutomationScript)
   {
      std::string scriptPath = ofToResourcePath("userdata_original/scripts/mpv_player_automation.py");
      if (!juce::File(scriptPath).existsAsFile())
         scriptPath = ofToDataPath("scripts/mpv_player_automation.py");
      LoadScriptFile(scriptPath);
      mLoadedAutomationScript = true;
   }
}

void MpvPlayerAutomation::CreateUIControls()
{
   ScriptModule::CreateUIControls();

   const int y = static_cast<int>(mHeight) + 2;
   mPlayerCountSlider = new IntSlider(this, "players", 3, y, kPlayerCountSliderWidth, kControlHeight, &mPlayerCount, 1, 16);
   mLayoutDropdown = new DropdownList(this, "layout", kPlayerCountSliderWidth + 10, y, &mLayout, kLayoutDropdownWidth);
   mLayoutDropdown->AddLabel("grid", (int)LayoutMode::Grid);
   mLayoutDropdown->AddLabel("strip", (int)LayoutMode::Strip);
   mLayoutDropdown->AddLabel("cascade", (int)LayoutMode::Cascade);
   mReserveCheckbox = new Checkbox(this, "reserve", kPlayerCountSliderWidth + kLayoutDropdownWidth + 18, y, &mReserveWindows);
   mTimelineCheckbox = new Checkbox(this, "timeline", kPlayerCountSliderWidth + kLayoutDropdownWidth + kReserveCheckboxWidth + 28, y, &mTimelineRunning);

   const int extraWidth = kPlayerCountSliderWidth + kLayoutDropdownWidth + kReserveCheckboxWidth + kTimelineCheckboxWidth + 38;
   if (mWidth < extraWidth)
      mWidth = extraWidth;
   mHeight += kControlHeight + 4;
}

void MpvPlayerAutomation::DrawScriptModuleExtras()
{
   if (mPlayerCountSlider)
      mPlayerCountSlider->Draw();
   if (mLayoutDropdown)
      mLayoutDropdown->Draw();
   if (mReserveCheckbox)
      mReserveCheckbox->Draw();
   if (mTimelineCheckbox)
      mTimelineCheckbox->Draw();
}

void MpvPlayerAutomation::CheckboxUpdated(Checkbox* checkbox, double time)
{
   IDrawableModule::CheckboxUpdated(checkbox, time);

   if (checkbox == mReserveCheckbox)
      RunCode(time, "reserve_windows(" + ofToString(mReserveWindows ? 1 : 0) + ")");
   if (checkbox == mTimelineCheckbox)
   {
      if (mTimelineRunning)
         RunCode(time, "start_timeline()");
      else
         RunCode(time, "timeline_running = False");
   }
}

void MpvPlayerAutomation::DropdownUpdated(DropdownList* list, int, double time)
{
   if (list == mLayoutDropdown)
      RunCode(time, "set_layout_mode(" + ofToString(mLayout) + ")");
}

void MpvPlayerAutomation::ResizeScriptModuleExtras(float, float heightDelta)
{
   if (mPlayerCountSlider)
      mPlayerCountSlider->SetPosition(mPlayerCountSlider->GetPosition(true).x, mPlayerCountSlider->GetPosition(true).y + heightDelta);
   if (mLayoutDropdown)
      mLayoutDropdown->SetPosition(mLayoutDropdown->GetPosition(true).x, mLayoutDropdown->GetPosition(true).y + heightDelta);
   if (mReserveCheckbox)
      mReserveCheckbox->SetPosition(mReserveCheckbox->GetPosition(true).x, mReserveCheckbox->GetPosition(true).y + heightDelta);
   if (mTimelineCheckbox)
      mTimelineCheckbox->SetPosition(mTimelineCheckbox->GetPosition(true).x, mTimelineCheckbox->GetPosition(true).y + heightDelta);
}
