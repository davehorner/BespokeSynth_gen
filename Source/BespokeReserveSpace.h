/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#pragma once

#include "IDrawableModule.h"
#include "ClickButton.h"
#include "DropdownList.h"
#include "Slider.h"

class BespokeReserveSpace : public IDrawableModule, public IDropdownListener, public IIntSliderListener, public IButtonListener
{
public:
   BespokeReserveSpace();
   ~BespokeReserveSpace() override;
   static IDrawableModule* Create() { return new BespokeReserveSpace(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void Poll() override;

   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void ButtonClicked(ClickButton* button, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 0; }

   float GetVideoX() const;
   float GetVideoY() const;
   float GetVideoW() const;
   float GetVideoH() const;

private:
   enum class Preset
   {
      Left = 0,
      Right,
      Middle,
      Custom
   };

   void DrawModule() override;
   void ApplyPreset();
   void ApplyBespokeWindowGeometry();
   ofRectangle GetScreenRect() const;
   ofRectangle GetReservedRect(bool staged) const;
   ofRectangle GetVideoRect(bool staged) const;

   DropdownList* mPresetDropdown{ nullptr };
   IntSlider* mXSlider{ nullptr };
   IntSlider* mYSlider{ nullptr };
   IntSlider* mWSlider{ nullptr };
   IntSlider* mHSlider{ nullptr };
   ClickButton* mApplyButton{ nullptr };

   int mPreset{ (int)Preset::Left };
   int mX{ 24 };
   int mY{ 36 };
   int mW{ 620 };
   int mH{ 760 };
   int mAppliedX{ 24 };
   int mAppliedY{ 36 };
   int mAppliedW{ 620 };
   int mAppliedH{ 760 };
   int mAppliedPreset{ (int)Preset::Left };
   bool mHasAppliedWindowGeometry{ false };
};
