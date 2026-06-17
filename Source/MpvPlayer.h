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
#include "ClickButton.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "TextEntry.h"

#include "juce_core/juce_core.h"

#include <limits>
#include <string>

class MpvPlayer : public IDrawableModule, public IButtonListener, public ITextEntryListener, public IFloatSliderListener, public IIntSliderListener
{
public:
   MpvPlayer();
   ~MpvPlayer() override;
   static IDrawableModule* Create() { return new MpvPlayer(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Poll() override;
   bool IsEnabled() const override { return true; }
   void OpenMedia(const std::string& mediaPath, bool play = true);

   void ButtonClicked(ClickButton* button, double time) override;
   void TextEntryComplete(TextEntry* entry) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 5; }

private:
   void DrawModule() override;
   void LaunchMpv();
   void StopMpv();
   void ApplyWindowGeometry();
   void SyncWindowGeometryFromNative();
   void ApplyMute();
   void PollPlaybackPosition();
   void ScrubToTime();
   void SetLoopPoint(bool start);
   void ClearLoop();
   void UpdateAnimation();
   void RandomizeAnimationVelocity();
   void JitterAnimationVelocity(bool hitX, bool hitY);
   juce::StringArray BuildMpvArgs() const;
   std::string GetWindowTitle() const;
   std::string GetIpcServerName() const;
   std::string ResolveMediaPath(const std::string& path) const;
   bool SendIpcCommand(const std::string& command, std::string* response = nullptr);
   bool GetMpvNumberProperty(const std::string& property, double& value);
   void SetStatus(const std::string& status);
   std::string FormatTime(double seconds) const;

   std::string mMpvExecutable{ "mpv" };
   std::string mMediaPath;
   bool mPlay{ false };
   bool mMute{ false };
   bool mAnimate{ false };
   float mAnimationSpeed{ 1.0f };
   float mOffscreenAmount{ 0.0f };
   float mTime{ 0.0f };
   double mDuration{ 0.0 };
   double mTimeSeconds{ 0.0 };
   double mLoopA{ -1.0 };
   double mLoopB{ -1.0 };
   float mWindowX{ 100.0f };
   float mWindowY{ 100.0f };
   int mWindowWidth{ 640 };
   int mWindowHeight{ 360 };
   float mVelocityX{ 5.0f };
   float mVelocityY{ 3.0f };
   std::string mStatus{ "idle" };
   double mLastAnimationTime{ -1 };
   double mLastGeometryApplyTime{ -100000.0 };
   double mLastNativeGeometryPollTime{ -100000.0 };
   double mLastPlaybackPollTime{ -100000.0 };
   float mLastAppliedWindowX{ std::numeric_limits<float>::quiet_NaN() };
   float mLastAppliedWindowY{ std::numeric_limits<float>::quiet_NaN() };
   int mLastAppliedWindowWidth{ -1 };
   int mLastAppliedWindowHeight{ -1 };

   TextEntry* mMpvEntry{ nullptr };
   TextEntry* mMediaEntry{ nullptr };
   Checkbox* mPlayCheckbox{ nullptr };
   Checkbox* mMuteCheckbox{ nullptr };
   ClickButton* mRestartButton{ nullptr };
   FloatSlider* mTimeSlider{ nullptr };
   ClickButton* mSetLoopAButton{ nullptr };
   ClickButton* mSetLoopBButton{ nullptr };
   ClickButton* mClearLoopButton{ nullptr };
   FloatSlider* mXSlider{ nullptr };
   FloatSlider* mYSlider{ nullptr };
   IntSlider* mWidthSlider{ nullptr };
   IntSlider* mHeightSlider{ nullptr };
   Checkbox* mAnimateCheckbox{ nullptr };
   FloatSlider* mAnimationSpeedSlider{ nullptr };
   FloatSlider* mOffscreenSlider{ nullptr };
   juce::ChildProcess mProcess;
};
