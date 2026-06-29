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
#include "DropdownList.h"
#include "IAudioReceiver.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "TextEntry.h"

#include "juce_core/juce_core.h"

#include <limits>
#include <string>

class MpvPlayer : public IDrawableModule, public IAudioReceiver, public IButtonListener, public ITextEntryListener, public IFloatSliderListener, public IIntSliderListener, public IDropdownListener
{
public:
   MpvPlayer();
   ~MpvPlayer() override;
   static IDrawableModule* Create() { return new MpvPlayer(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Exit() override;
   void Poll() override;
   bool IsEnabled() const override { return true; }
   void OpenMedia(const std::string& mediaPath, bool play = true);
   void UnloadMedia();
   void SetPlayback(bool play);
   void SetMute(bool mute);
   void SetVolume(float volume);
   void SetTimeSeconds(double seconds);
   void SetWindowGeometry(float x, float y, int width, int height);
   void SetWindowPreset(int preset);
   double GetTimeSeconds() const { return mTimeSeconds; }
   double GetDurationSeconds() const { return mDuration; }
   float GetWindowX() const { return mWindowX; }
   float GetWindowY() const { return mWindowY; }
   int GetWindowWidth() const { return mWindowWidth; }
   int GetWindowHeight() const { return mWindowHeight; }

   void ButtonClicked(ClickButton* button, double time) override;
   void TextEntryComplete(TextEntry* entry) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 8; }

private:
   enum class CropMode
   {
      Fit = 0,
      Fill = 1,
      Cover = 2,
      Manual = 3
   };

   void DrawModule() override;
   void LaunchMpv();
   void StopMpv();
   void ApplyWindowGeometry();
   void SyncWindowGeometryFromNative();
   void ApplyMute();
   void ApplyPlaybackPause();
   void ApplyVolume();
   void ApplyRate();
   void ApplyTimeSeek();
   void ApplyLoopPoints();
   void EnforceLoopPoints();
   void ApplyCropFilter();
   void ApplyWindowPreset(int preset);
   void DuplicateModule();
   void CopySettingsTo(MpvPlayer& player) const;
   void PlaceAwayFromOtherMpvWindows();
   void SyncGeometryControls();
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
   bool GetMpvBoolProperty(const std::string& property, bool& value);
   void SetStatus(const std::string& status);
   std::string FormatTime(double seconds) const;

   std::string mMpvExecutable{ "mpv" };
   std::string mMediaPath;
   bool mPlay{ false };
   bool mPlaybackPauseNeedsApply{ false };
   bool mMute{ false };
   float mVolume{ 100.0f };
   bool mVolumeNeedsApply{ false };
   float mRate{ 1.0f };
   bool mRateNeedsApply{ false };
   bool mAnimate{ false };
   float mAnimationSpeed{ 1.0f };
   float mOffscreenAmount{ 0.5f };
   int mCropMode{ (int)CropMode::Fit };
   bool mCropNeedsApply{ false };
   std::string mLastCropFilter;
   float mCropX{ 0.0f };
   float mCropY{ 0.0f };
   float mCropW{ 0.0f };
   float mCropH{ 0.0f };
   float mTime{ 0.0f };
   double mDuration{ 0.0 };
   int mVideoWidth{ 0 };
   int mVideoHeight{ 0 };
   float mDurationControl{ 0.0f };
   double mTimeSeconds{ 0.0 };
   bool mTimeNeedsApply{ false };
   bool mWaitingForTimeSeekAck{ false };
   double mTimeSeekTarget{ 0.0 };
   double mTimeSeekStartTime{ -100000.0 };
   double mLoopA{ -1.0 };
   double mLoopB{ -1.0 };
   float mLoopAControl{ -1.0f };
   float mLoopBControl{ -1.0f };
   bool mLoopNeedsApply{ false };
   double mLastLoopEnforceTime{ -100000.0 };
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
   FloatSlider* mVolumeSlider{ nullptr };
   FloatSlider* mRateSlider{ nullptr };
   ClickButton* mRestartButton{ nullptr };
   ClickButton* mCloseButton{ nullptr };
   ClickButton* mDuplicateButton{ nullptr };
   FloatSlider* mTimeSlider{ nullptr };
   FloatSlider* mDurationSlider{ nullptr };
   ClickButton* mSetLoopAButton{ nullptr };
   ClickButton* mSetLoopBButton{ nullptr };
   ClickButton* mClearLoopButton{ nullptr };
   FloatSlider* mLoopASlider{ nullptr };
   FloatSlider* mLoopBSlider{ nullptr };
   FloatSlider* mXSlider{ nullptr };
   FloatSlider* mYSlider{ nullptr };
   IntSlider* mWidthSlider{ nullptr };
   IntSlider* mHeightSlider{ nullptr };
   Checkbox* mAnimateCheckbox{ nullptr };
   FloatSlider* mAnimationSpeedSlider{ nullptr };
   FloatSlider* mOffscreenSlider{ nullptr };
   DropdownList* mCropModeDropdown{ nullptr };
   FloatSlider* mCropXSlider{ nullptr };
   FloatSlider* mCropYSlider{ nullptr };
   FloatSlider* mCropWSlider{ nullptr };
   FloatSlider* mCropHSlider{ nullptr };
   ClickButton* mPresetCenterButton{ nullptr };
   ClickButton* mPresetFullButton{ nullptr };
   ClickButton* mPresetGridButton{ nullptr };
   ClickButton* mPresetStripButton{ nullptr };
   ClickButton* mPresetCascadeButton{ nullptr };
   juce::ChildProcess mProcess;
};
