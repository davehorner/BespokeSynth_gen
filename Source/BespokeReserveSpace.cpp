/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "BespokeReserveSpace.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "UIControlMacros.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
   constexpr int kDefaultScreenW = 5120;
   constexpr int kDefaultScreenH = 1440;
   constexpr float kPreviewW = 220.0f;
   constexpr float kPreviewH = 124.0f;
}

BespokeReserveSpace::BespokeReserveSpace()
: IDrawableModule(260, 160)
{
}

BespokeReserveSpace::~BespokeReserveSpace() = default;

void BespokeReserveSpace::Init()
{
   IDrawableModule::Init();
}

void BespokeReserveSpace::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   DROPDOWN(mPresetDropdown, "reserve", &mPreset, 88);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mXSlider, "x", &mX, -8000, 8000);
   UIBLOCK_NEWLINE();
   INTSLIDER(mYSlider, "y", &mY, -4000, 4000);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mWSlider, "w", &mW, 160, 8192);
   UIBLOCK_NEWLINE();
   INTSLIDER(mHSlider, "h", &mH, 90, 4096);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mApplyButton, "apply");
   ENDUIBLOCK(mWidth, mHeight);

   mPresetDropdown->AddLabel("left", (int)Preset::Left);
   mPresetDropdown->AddLabel("right", (int)Preset::Right);
   mPresetDropdown->AddLabel("middle", (int)Preset::Middle);
   mPresetDropdown->AddLabel("custom", (int)Preset::Custom);

   mHeight += kPreviewH + 16;
   if (mWidth < kPreviewW + 14)
      mWidth = kPreviewW + 14;
}

void BespokeReserveSpace::Poll()
{
}

void BespokeReserveSpace::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mPresetDropdown->Draw();
   mXSlider->Draw();
   mYSlider->Draw();
   mWSlider->Draw();
   mHSlider->Draw();
   mApplyButton->Draw();

   const ofRectangle screen = GetScreenRect();
   const ofRectangle reserved = GetReservedRect(true);
   const ofRectangle video = GetVideoRect(true);
   const float scale = std::min(kPreviewW / std::max(1.0f, screen.width), kPreviewH / std::max(1.0f, screen.height));
   const float px = 8.0f;
   const float py = mHeight - kPreviewH - 8.0f;

   auto mapRect = [&](const ofRectangle& rect)
   {
      return ofRectangle(px + (rect.x - screen.x) * scale,
                         py + (rect.y - screen.y) * scale,
                         rect.width * scale,
                         rect.height * scale);
   };

   ofPushStyle();
   ofNoFill();
   ofSetColor(255, 255, 255, 100);
   ofRect(px, py, screen.width * scale, screen.height * scale);

   ofFill();
   ofSetColor(80, 180, 255, 80);
   ofRect(mapRect(video));
   ofSetColor(255, 180, 60, 120);
   ofRect(mapRect(reserved));

   ofNoFill();
   ofSetColor(255, 180, 60, 220);
   ofRect(mapRect(reserved));
   ofSetColor(80, 180, 255, 220);
   ofRect(mapRect(video));
   ofPopStyle();
}

void BespokeReserveSpace::DropdownUpdated(DropdownList* list, int, double)
{
   if (list == mPresetDropdown)
   {
      ApplyPreset();
   }
}

void BespokeReserveSpace::IntSliderUpdated(IntSlider* slider, int, double)
{
   if (slider == mXSlider || slider == mYSlider || slider == mWSlider || slider == mHSlider)
   {
      mPreset = (int)Preset::Custom;
   }
}

void BespokeReserveSpace::ButtonClicked(ClickButton* button, double)
{
   if (button == mApplyButton)
      ApplyBespokeWindowGeometry();
}

void BespokeReserveSpace::ApplyPreset()
{
   const ofRectangle screen = GetScreenRect();
   const int margin = 24;
   const int top = 36;
   const int height = std::max(90, (int)std::round(screen.height * 0.86f));
   const int sideW = std::max(720, (int)std::round(screen.width * 0.255f));
   const int midW = std::max(960, (int)std::round(screen.width * 0.34f));

   if (mPreset == (int)Preset::Left)
   {
      mX = (int)screen.x + margin;
      mY = (int)screen.y + top;
      mW = sideW;
      mH = height;
   }
   else if (mPreset == (int)Preset::Right)
   {
      mX = (int)std::round(screen.getMaxX()) - sideW - margin;
      mY = (int)screen.y + top;
      mW = sideW;
      mH = height;
   }
   else if (mPreset == (int)Preset::Middle)
   {
      mX = (int)std::round(screen.x + (screen.width - midW) * 0.5f);
      mY = (int)screen.y + top;
      mW = midW;
      mH = height;
   }
}

void BespokeReserveSpace::ApplyBespokeWindowGeometry()
{
   const ofRectangle screen = GetScreenRect();
   const int w = std::min(std::max(160, mW), std::max(160, (int)screen.width));
   const int h = std::min(std::max(90, mH), std::max(90, (int)screen.height));
   const int x = std::clamp(mX, (int)screen.x, (int)std::round(screen.getMaxX()) - w);
   const int y = std::clamp(mY, (int)screen.y, (int)std::round(screen.getMaxY()) - h);

   mX = x;
   mY = y;
   mW = w;
   mH = h;
   mAppliedX = mX;
   mAppliedY = mY;
   mAppliedW = mW;
   mAppliedH = mH;
   mAppliedPreset = mPreset;
   if (TheSynth != nullptr && TheSynth->GetMainComponent() != nullptr && TheSynth->GetMainComponent()->getTopLevelComponent() != nullptr)
      TheSynth->GetMainComponent()->getTopLevelComponent()->setBounds(mX, mY, mW, mH);
   mHasAppliedWindowGeometry = true;
}

ofRectangle BespokeReserveSpace::GetScreenRect() const
{
   if (TheSynth != nullptr && TheSynth->GetMainComponent() != nullptr)
   {
      if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(TheSynth->GetMainComponent()->getScreenBounds()))
      {
         const auto& area = display->userArea;
         return ofRectangle((float)area.getX(), (float)area.getY(), (float)area.getWidth(), (float)area.getHeight());
      }
   }

   const auto area = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
   if (area.getWidth() > 0 && area.getHeight() > 0)
      return ofRectangle((float)area.getX(), (float)area.getY(), (float)area.getWidth(), (float)area.getHeight());

   return ofRectangle(0, 0, kDefaultScreenW, kDefaultScreenH);
}

ofRectangle BespokeReserveSpace::GetReservedRect(bool staged) const
{
   const ofRectangle screen = GetScreenRect();
   const int sourceX = staged ? mX : mAppliedX;
   const int sourceY = staged ? mY : mAppliedY;
   const int sourceW = staged ? mW : mAppliedW;
   const int sourceH = staged ? mH : mAppliedH;
   const float w = std::min((float)std::max(160, sourceW), screen.width);
   const float h = std::min((float)std::max(90, sourceH), screen.height);
   const float x = std::clamp((float)sourceX, screen.x, screen.getMaxX() - w);
   const float y = std::clamp((float)sourceY, screen.y, screen.getMaxY() - h);
   return ofRectangle(x, y, w, h);
}

ofRectangle BespokeReserveSpace::GetVideoRect(bool staged) const
{
   const ofRectangle screen = GetScreenRect();
   const ofRectangle reserved = GetReservedRect(staged);
   std::vector<ofRectangle> candidates;

   candidates.emplace_back(screen.x, screen.y, std::max(0.0f, reserved.x - screen.x), screen.height);
   candidates.emplace_back(reserved.getMaxX(), screen.y, std::max(0.0f, screen.getMaxX() - reserved.getMaxX()), screen.height);
   candidates.emplace_back(screen.x, screen.y, screen.width, std::max(0.0f, reserved.y - screen.y));
   candidates.emplace_back(screen.x, reserved.getMaxY(), screen.width, std::max(0.0f, screen.getMaxY() - reserved.getMaxY()));

   ofRectangle best = screen;
   float bestArea = -1.0f;
   for (const auto& candidate : candidates)
   {
      const float area = candidate.width * candidate.height;
      if (area > bestArea)
      {
         best = candidate;
         bestArea = area;
      }
   }

   const float margin = 24.0f;
   best.x += margin;
   best.y += margin;
   best.width = std::max(160.0f, best.width - margin * 2.0f);
   best.height = std::max(90.0f, best.height - margin * 2.0f);
   return best;
}

float BespokeReserveSpace::GetVideoX() const
{
   return GetVideoRect(false).x;
}

float BespokeReserveSpace::GetVideoY() const
{
   return GetVideoRect(false).y;
}

float BespokeReserveSpace::GetVideoW() const
{
   return GetVideoRect(false).width;
}

float BespokeReserveSpace::GetVideoH() const
{
   return GetVideoRect(false).height;
}

void BespokeReserveSpace::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadInt("preset", moduleInfo, (int)Preset::Left, (int)Preset::Left, (int)Preset::Custom);
   mModuleSaveData.LoadInt("x", moduleInfo, 24, -8000, 8000);
   mModuleSaveData.LoadInt("y", moduleInfo, 36, -4000, 4000);
   mModuleSaveData.LoadInt("w", moduleInfo, 1306, 160, 8192);
   mModuleSaveData.LoadInt("h", moduleInfo, 1238, 90, 4096);
   SetUpFromSaveData();
}

void BespokeReserveSpace::SetUpFromSaveData()
{
   mPreset = mModuleSaveData.GetInt("preset");
   mX = mModuleSaveData.GetInt("x");
   mY = mModuleSaveData.GetInt("y");
   mW = mModuleSaveData.GetInt("w");
   mH = mModuleSaveData.GetInt("h");
   if (mPreset != (int)Preset::Custom)
      ApplyPreset();
   ApplyBespokeWindowGeometry();
}

void BespokeReserveSpace::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["preset"] = mAppliedPreset;
   moduleInfo["x"] = mAppliedX;
   moduleInfo["y"] = mAppliedY;
   moduleInfo["w"] = mAppliedW;
   moduleInfo["h"] = mAppliedH;
}

void BespokeReserveSpace::SaveState(FileStreamOut& out)
{
   out << GetModuleSaveStateRev();
   IDrawableModule::SaveState(out);
}

void BespokeReserveSpace::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   LoadStateValidate(rev <= GetModuleSaveStateRev());
}
