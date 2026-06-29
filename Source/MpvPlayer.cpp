/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "MpvPlayer.h"
#include "FileStream.h"
#include "ModularSynth.h"
#include "ModuleFactory.h"
#include "ofxJSONElement.h"
#include "SynthGlobals.h"
#include "UIControlMacros.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

#if BESPOKE_WINDOWS
#include <windows.h>
#undef LoadString
#undef min
#undef max
#endif

#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace
{
struct DesktopBounds
{
   float minX{ 0.0f };
   float minY{ 0.0f };
   float maxX{ 1920.0f };
   float maxY{ 1080.0f };
};

struct WindowGeometry
{
   int x{ 0 };
   int y{ 0 };
   int width{ 1 };
   int height{ 1 };
};

float RectOverlapArea(const ofRectangle& a, const ofRectangle& b)
{
   const float left = std::max(a.getMinX(), b.getMinX());
   const float right = std::min(a.getMaxX(), b.getMaxX());
   const float top = std::max(a.getMinY(), b.getMinY());
   const float bottom = std::min(a.getMaxY(), b.getMaxY());
   return std::max(0.0f, right - left) * std::max(0.0f, bottom - top);
}

DesktopBounds GetDesktopBounds()
{
   const juce::Rectangle<int> bounds = juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
   if (!bounds.isEmpty())
      return { (float)bounds.getX(), (float)bounds.getY(), (float)bounds.getRight(), (float)bounds.getBottom() };

   return {};
}

WindowGeometry GetNativeWindowGeometry(float x, float y, int width, int height)
{
   const juce::Rectangle<int> logicalRect((int)std::round(x), (int)std::round(y), std::max(1, width), std::max(1, height));

#if BESPOKE_WINDOWS || JUCE_LINUX || JUCE_BSD
   const juce::Rectangle<int> nativeRect = juce::Desktop::getInstance().getDisplays().logicalToPhysical(logicalRect);
   if (!nativeRect.isEmpty())
      return { nativeRect.getX(), nativeRect.getY(), std::max(1, nativeRect.getWidth()), std::max(1, nativeRect.getHeight()) };
#endif

   return { logicalRect.getX(), logicalRect.getY(), logicalRect.getWidth(), logicalRect.getHeight() };
}

void RunShortProcess(const juce::StringArray& args, int timeoutMs = 150)
{
   juce::ChildProcess process;
   if (process.start(args, 0))
      process.waitForProcessToFinish(timeoutMs);
}

juce::String ToAppleScriptString(const std::string& text)
{
   juce::String escaped(text);
   escaped = escaped.replace("\\", "\\\\").replace("\"", "\\\"");
   return "\"" + escaped + "\"";
}

std::string FormatMpvNumber(double value)
{
   return juce::String(value, 6).toStdString();
}
}

MpvPlayer::MpvPlayer()
: IAudioReceiver(gBufferSize)
{
}

MpvPlayer::~MpvPlayer()
{
   StopMpv();
}

void MpvPlayer::Exit()
{
   StopMpv();
   IDrawableModule::Exit();
}

void MpvPlayer::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK(3, 3, 220);
   TEXTENTRY(mMpvEntry, "mpv", 80, &mMpvExecutable);
   TEXTENTRY(mMediaEntry, "media", 90, &mMediaPath);
   CHECKBOX(mPlayCheckbox, "play", &mPlay);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mMuteCheckbox, "mute", &mMute);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(84);
   FLOATSLIDER_DIGITS(mVolumeSlider, "vol", &mVolume, 0.0f, 130.0f, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER_DIGITS(mRateSlider, "rate", &mRate, 0.1f, 4.0f, 2);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_NEWLINE();
   BUTTON(mRestartButton, "restart");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mCloseButton, "close");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mDuplicateButton, "dup");
   UIBLOCK_NEWLINE();
   FLOATSLIDER_DIGITS(mTimeSlider, "time", &mTime, 0, 7200, 3);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER_DIGITS(mDurationSlider, "duration", &mDurationControl, 0, 7200, 3);
   UIBLOCK_NEWLINE();
   BUTTON(mSetLoopAButton, "set a");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mSetLoopBButton, "set b");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mClearLoopButton, "clear ab");
   UIBLOCK_NEWLINE();
   UIBLOCK_PUSHSLIDERWIDTH(105);
   FLOATSLIDER_DIGITS(mLoopASlider, "a", &mLoopAControl, -1, 7200, 3);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER_DIGITS(mLoopBSlider, "b", &mLoopBControl, -1, 7200, 3);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_NEWLINE();
   UIBLOCK_PUSHSLIDERWIDTH(64);
   FLOATSLIDER(mXSlider, "x", &mWindowX, -4000, 4000);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mYSlider, "y", &mWindowY, -4000, 4000);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mWidthSlider, "w", &mWindowWidth, 1, 4096);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mHeightSlider, "h", &mWindowHeight, 1, 4096);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_NEWLINE();
   CHECKBOX(mAnimateCheckbox, "animate", &mAnimate);
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(90);
   FLOATSLIDER_DIGITS(mAnimationSpeedSlider, "speed", &mAnimationSpeed, 0.01f, 1.0f, 2);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER_DIGITS(mOffscreenSlider, "offscreen", &mOffscreenAmount, 0.0f, 1.0f, 2);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_NEWLINE();
   DROPDOWN(mCropModeDropdown, "crop", &mCropMode, 90);
   mCropModeDropdown->AddLabel("fit", (int)CropMode::Fit);
   mCropModeDropdown->AddLabel("fill", (int)CropMode::Fill);
   mCropModeDropdown->AddLabel("cover", (int)CropMode::Cover);
   mCropModeDropdown->AddLabel("manual", (int)CropMode::Manual);
   UIBLOCK_NEWLINE();
   UIBLOCK_PUSHSLIDERWIDTH(64);
   FLOATSLIDER(mCropXSlider, "crop x", &mCropX, 0, 4096);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mCropYSlider, "crop y", &mCropY, 0, 4096);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mCropWSlider, "crop w", &mCropW, 0, 4096);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mCropHSlider, "crop h", &mCropH, 0, 4096);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_NEWLINE();
   BUTTON(mPresetCenterButton, "center");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mPresetFullButton, "full");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mPresetGridButton, "grid");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mPresetStripButton, "strip");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mPresetCascadeButton, "cascade");
   ENDUIBLOCK(mWidth, mHeight);

   mHeight += 20;
}

void MpvPlayer::Poll()
{
   if (mPlay && !mProcess.isRunning())
      LaunchMpv();

   ApplyPlaybackPause();
   ApplyVolume();
   ApplyRate();

   ApplyWindowGeometry();
   SyncWindowGeometryFromNative();
   UpdateAnimation();
   ApplyWindowGeometry();
   ApplyTimeSeek();
   ApplyLoopPoints();
   EnforceLoopPoints();
   PollPlaybackPosition();
   ApplyCropFilter();
}

void MpvPlayer::OpenMedia(const std::string& mediaPath, bool play)
{
   mMediaPath = mediaPath;
   mPlay = play;
   if (mMediaEntry != nullptr)
      mMediaEntry->SetText(mMediaPath);
   if (mPlayCheckbox != nullptr)
      mPlayCheckbox->SetValue(mPlay ? 1.0f : 0.0f, gTime, false);

   LaunchMpv();
}

void MpvPlayer::UnloadMedia()
{
   mPlay = false;
   mMediaPath.clear();
   if (mPlayCheckbox != nullptr)
      mPlayCheckbox->SetValue(0.0f, gTime, false);
   if (mMediaEntry != nullptr)
      mMediaEntry->SetText("");
   StopMpv();
   SetStatus("unloaded");
}

void MpvPlayer::SetPlayback(bool play)
{
   mPlay = play;
   if (mPlayCheckbox != nullptr)
      mPlayCheckbox->SetValue(mPlay ? 1.0f : 0.0f, gTime, false);
   if (mPlay && !mProcess.isRunning())
      LaunchMpv();
   else
   {
      mPlaybackPauseNeedsApply = true;
      ApplyPlaybackPause();
   }
}

void MpvPlayer::SetMute(bool mute)
{
   mMute = mute;
   if (mMuteCheckbox != nullptr)
      mMuteCheckbox->SetValue(mMute ? 1.0f : 0.0f, gTime, false);
   ApplyMute();
}

void MpvPlayer::SetVolume(float volume)
{
   mVolume = std::clamp(volume, 0.0f, 130.0f);
   if (mVolumeSlider != nullptr)
      mVolumeSlider->SetValue(mVolume, gTime, false);
   mVolumeNeedsApply = true;
   ApplyVolume();
}

void MpvPlayer::SetTimeSeconds(double seconds)
{
   mTimeSeconds = std::max(0.0, seconds);
   if (mDuration > 0.0)
      mTimeSeconds = std::min(mTimeSeconds, mDuration);
   mTime = (float)mTimeSeconds;
   if (mTimeSlider != nullptr && !mTimeSlider->IsMouseDown())
      mTimeSlider->SetValue(mTime, gTime, false);
   mTimeNeedsApply = true;
   ApplyTimeSeek();
}

void MpvPlayer::SetWindowGeometry(float x, float y, int width, int height)
{
   mWindowX = x;
   mWindowY = y;
   mWindowWidth = std::max(1, width);
   mWindowHeight = std::max(1, height);
   mCropNeedsApply = true;
   SyncGeometryControls();
   ApplyWindowGeometry();
   ApplyCropFilter();
}

void MpvPlayer::SetWindowPreset(int preset)
{
   ApplyWindowPreset(preset);
}

void MpvPlayer::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mMpvEntry->Draw();
   mMediaEntry->Draw();
   mPlayCheckbox->Draw();
   mMuteCheckbox->Draw();
   mVolumeSlider->Draw();
   mRateSlider->Draw();
   mRestartButton->Draw();
   mCloseButton->Draw();
   mDuplicateButton->Draw();
   mTimeSlider->Draw();
   mDurationSlider->Draw();
   mSetLoopAButton->Draw();
   mSetLoopBButton->Draw();
   mClearLoopButton->Draw();
   mLoopASlider->Draw();
   mLoopBSlider->Draw();
   mXSlider->Draw();
   mYSlider->Draw();
   mWidthSlider->Draw();
   mHeightSlider->Draw();
   mAnimateCheckbox->Draw();
   mAnimationSpeedSlider->Draw();
   mOffscreenSlider->Draw();
   mCropModeDropdown->Draw();
   mCropXSlider->Draw();
   mCropYSlider->Draw();
   mCropWSlider->Draw();
   mCropHSlider->Draw();
   mPresetCenterButton->Draw();
   mPresetFullButton->Draw();
   mPresetGridButton->Draw();
   mPresetStripButton->Draw();
   mPresetCascadeButton->Draw();
   DrawTextNormal(mStatus, 3, mHeight - 5);
}

void MpvPlayer::ButtonClicked(ClickButton* button, double)
{
   if (button == mRestartButton)
      LaunchMpv();
   if (button == mCloseButton)
   {
      mPlay = false;
      if (mPlayCheckbox)
         mPlayCheckbox->SetValue(0.0f, gTime, false);
      StopMpv();
      SetStatus("closed");
   }
   if (button == mDuplicateButton)
      DuplicateModule();
   if (button == mSetLoopAButton)
      SetLoopPoint(true);
   if (button == mSetLoopBButton)
      SetLoopPoint(false);
   if (button == mClearLoopButton)
      ClearLoop();
   if (button == mPresetCenterButton)
      ApplyWindowPreset(0);
   if (button == mPresetFullButton)
      ApplyWindowPreset(1);
   if (button == mPresetGridButton)
      ApplyWindowPreset(2);
   if (button == mPresetStripButton)
      ApplyWindowPreset(3);
   if (button == mPresetCascadeButton)
      ApplyWindowPreset(4);
}

void MpvPlayer::TextEntryComplete(TextEntry*)
{
   if (mPlay)
      LaunchMpv();
}

void MpvPlayer::FloatSliderUpdated(FloatSlider* slider, float, double)
{
   if (slider == mTimeSlider)
      ScrubToTime();
   else if (slider == mLoopASlider || slider == mLoopBSlider)
   {
      mLoopA = mLoopAControl;
      mLoopB = mLoopBControl;
      mLoopNeedsApply = true;
      ApplyLoopPoints();
   }
   else if (slider == mDurationSlider)
      return;
   else if (slider == mVolumeSlider)
   {
      mVolumeNeedsApply = true;
      ApplyVolume();
   }
   else if (slider == mRateSlider)
   {
      mRateNeedsApply = true;
      ApplyRate();
   }
   else if (slider == mCropXSlider || slider == mCropYSlider || slider == mCropWSlider || slider == mCropHSlider)
   {
      mCropMode = (int)CropMode::Manual;
      if (mCropModeDropdown)
         mCropModeDropdown->SetValue((float)mCropMode, gTime, false);
      mCropNeedsApply = true;
      ApplyCropFilter();
   }
   else if (slider == mAnimationSpeedSlider || slider == mOffscreenSlider)
      return;
   else
   {
      mCropNeedsApply = true;
      ApplyWindowGeometry();
   }
}

void MpvPlayer::IntSliderUpdated(IntSlider*, int, double)
{
   mCropNeedsApply = true;
   ApplyWindowGeometry();
}

void MpvPlayer::CheckboxUpdated(Checkbox* checkbox, double)
{
   if (checkbox == mPlayCheckbox)
   {
      if (mPlay)
      {
         if (!mProcess.isRunning())
            LaunchMpv();
         else
         {
            mPlaybackPauseNeedsApply = true;
            ApplyPlaybackPause();
         }
      }
      else
      {
         mPlaybackPauseNeedsApply = true;
         ApplyPlaybackPause();
      }
   }

   if (checkbox == mAnimateCheckbox)
   {
      mLastAnimationTime = gTime;
      if (mAnimate)
         RandomizeAnimationVelocity();
   }

   if (checkbox == mMuteCheckbox)
      ApplyMute();
}

void MpvPlayer::DropdownUpdated(DropdownList* list, int, double)
{
   if (list == mCropModeDropdown)
   {
      mCropNeedsApply = true;
      ApplyCropFilter();
   }
}

void MpvPlayer::LaunchMpv()
{
   StopMpv();

   if (mMediaPath.empty())
   {
      SetStatus("set media path/url");
      return;
   }

   juce::StringArray args = BuildMpvArgs();
#if JUCE_MAC || JUCE_LINUX || JUCE_BSD
   juce::File(GetIpcServerName()).deleteFile();
#endif

   if (!mProcess.start(args))
   {
      SetStatus("failed to launch mpv");
      mPlay = false;
      if (mPlayCheckbox)
         mPlayCheckbox->SetValue(0.0f, gTime, false);
      return;
   }

   SetStatus("playing");
   mLastGeometryApplyTime = -100000.0;
   mLastAppliedWindowX = std::numeric_limits<float>::quiet_NaN();
   ApplyWindowGeometry();
   ApplyMute();
   mPlaybackPauseNeedsApply = true;
   mVolumeNeedsApply = true;
   mRateNeedsApply = true;
   mTimeNeedsApply = mTimeSeconds > 0.0;
   mLoopNeedsApply = mLoopA >= 0.0 || mLoopB >= 0.0;
   mCropNeedsApply = true;
   mLastCropFilter.clear();
   ApplyTimeSeek();
   ApplyLoopPoints();
   ApplyCropFilter();
}

void MpvPlayer::StopMpv()
{
   if (!mProcess.isRunning())
      return;

   SendIpcCommand("{\"command\":[\"quit\"]}\n");
   if (!mProcess.waitForProcessToFinish(500))
      mProcess.kill();
   mPlaybackPauseNeedsApply = false;
}

juce::StringArray MpvPlayer::BuildMpvArgs() const
{
   juce::StringArray args;
   args.add(mMpvExecutable);
   args.add("--force-window=yes");
   args.add("--ontop=yes");
   args.add("--no-terminal");
   args.add("--input-ipc-server=" + juce::String(GetIpcServerName()));
   args.add(mPlay ? "--pause=no" : "--pause=yes");
   args.add(mMute ? "--mute=yes" : "--mute=no");
   args.add("--volume=" + juce::String(mVolume, 1));
   args.add("--speed=" + juce::String(mRate, 3));
   args.add("--title=" + juce::String(GetWindowTitle()));
   const WindowGeometry geometry = GetNativeWindowGeometry(mWindowX, mWindowY, mWindowWidth, mWindowHeight);
   args.add("--geometry=" + juce::String(geometry.width) + "x" + juce::String(geometry.height) + "+" + juce::String(geometry.x) + "+" + juce::String(geometry.y));
   if (mTimeSeconds > 0.0)
      args.add("--start=" + juce::String(mTimeSeconds, 6));
   if (juce::URL::isProbablyAWebsiteURL(mMediaPath))
   {
      args.add("--script-opts=ytdl_hook-ytdl_path=yt-dlp");
      args.add("--ytdl-format=bestvideo*+bestaudio/best");
   }
   args.add(ResolveMediaPath(mMediaPath));
   return args;
}

void MpvPlayer::ApplyMute()
{
   if (!mProcess.isRunning())
      return;

   const std::string command = std::string("{\"command\":[\"set_property\",\"mute\",") + (mMute ? "true" : "false") + "]}\n";
   if (!SendIpcCommand(command))
      SetStatus("playing (mute ipc unavailable)");
}

void MpvPlayer::ApplyPlaybackPause()
{
   if (!mPlaybackPauseNeedsApply || !mProcess.isRunning())
      return;

   const bool paused = !mPlay;
   const std::string command = std::string("{\"command\":[\"set_property\",\"pause\",") + (paused ? "true" : "false") + "]}\n";
   if (SendIpcCommand(command))
   {
      mPlaybackPauseNeedsApply = false;
      SetStatus(paused ? "paused" : "playing");
   }
   else
      SetStatus("playing (pause ipc unavailable)");
}

void MpvPlayer::ApplyVolume()
{
   if (!mVolumeNeedsApply || !mProcess.isRunning())
      return;

   if (SendIpcCommand("{\"command\":[\"set_property\",\"volume\"," + FormatMpvNumber(mVolume) + "]}\n"))
      mVolumeNeedsApply = false;
   else
      SetStatus("playing (volume ipc unavailable)");
}

void MpvPlayer::ApplyRate()
{
   if (!mRateNeedsApply || !mProcess.isRunning())
      return;

   if (SendIpcCommand("{\"command\":[\"set_property\",\"speed\"," + FormatMpvNumber(mRate) + "]}\n"))
      mRateNeedsApply = false;
   else
      SetStatus("playing (rate ipc unavailable)");
}

void MpvPlayer::ApplyTimeSeek()
{
   if (!mTimeNeedsApply || !mProcess.isRunning())
      return;

   if (SendIpcCommand("{\"command\":[\"seek\"," + FormatMpvNumber(mTimeSeconds) + ",\"absolute+exact\"]}\n"))
   {
      mTimeNeedsApply = false;
      mWaitingForTimeSeekAck = true;
      mTimeSeekTarget = mTimeSeconds;
      mTimeSeekStartTime = gTime;
   }
}

void MpvPlayer::ApplyLoopPoints()
{
   if (!mLoopNeedsApply || !mProcess.isRunning())
      return;

   const bool hasLoop = mLoopA >= 0.0 && mLoopB >= 0.0 && std::abs(mLoopB - mLoopA) > 0.001;
   const double loopStart = hasLoop ? std::min(mLoopA, mLoopB) : mLoopA;
   const double loopEnd = hasLoop ? std::max(mLoopA, mLoopB) : mLoopB;

   bool ok = true;
   if (loopStart >= 0.0)
      ok &= SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-a\"," + FormatMpvNumber(loopStart) + "]}\n");
   else
      ok &= SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-a\",\"no\"]}\n");
   if (loopEnd >= 0.0)
      ok &= SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-b\"," + FormatMpvNumber(loopEnd) + "]}\n");
   else
      ok &= SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-b\",\"no\"]}\n");

   if (ok)
      mLoopNeedsApply = false;
}

void MpvPlayer::EnforceLoopPoints()
{
   if (!mProcess.isRunning() || !mPlay || mWaitingForTimeSeekAck)
      return;
   if (mLoopA < 0.0 || mLoopB < 0.0)
      return;

   const double loopStart = std::min(mLoopA, mLoopB);
   const double loopEnd = std::max(mLoopA, mLoopB);
   if (loopEnd - loopStart <= 0.001)
      return;

   const double minIntervalMs = std::clamp(80.0 / std::max(0.1f, mRate), 20.0, 120.0);
   if (gTime - mLastLoopEnforceTime < minIntervalMs)
      return;
   mLastLoopEnforceTime = gTime;

   double timeSeconds = mTimeSeconds;
   if (!GetMpvNumberProperty("time-pos", timeSeconds))
      return;

   const double toleranceSeconds = std::clamp((gTime - mLastPlaybackPollTime) * 0.001 * std::max(0.1f, mRate), 0.02, 0.18);
   if (timeSeconds >= loopEnd - toleranceSeconds || timeSeconds < loopStart - 0.25)
   {
      mTimeSeconds = loopStart;
      mTime = (float)std::clamp(mTimeSeconds, 0.0, std::max(1.0, mDuration));
      if (mTimeSlider && !mTimeSlider->IsMouseDown())
         mTimeSlider->SetValue(mTime, gTime, false);
      mTimeNeedsApply = true;
      ApplyTimeSeek();
   }
   else
   {
      mTimeSeconds = timeSeconds;
   }
}

void MpvPlayer::ApplyCropFilter()
{
   if (!mCropNeedsApply || !mProcess.isRunning())
      return;

   std::string filter;
   if (mCropMode == (int)CropMode::Fill || mCropMode == (int)CropMode::Cover)
   {
      if (mVideoWidth <= 0 || mVideoHeight <= 0 || mWindowWidth <= 0 || mWindowHeight <= 0)
         return;

      int cropW = mVideoWidth;
      int cropH = mVideoHeight;
      int cropX = 0;
      int cropY = 0;

      if (mCropMode == (int)CropMode::Fill)
      {
         const double sourceAspect = (double)mVideoWidth / (double)mVideoHeight;
         const double windowAspect = (double)mWindowWidth / (double)mWindowHeight;
         if (windowAspect > sourceAspect)
            cropH = std::max(1, (int)std::round((double)mVideoWidth / windowAspect));
         else
            cropW = std::max(1, (int)std::round((double)mVideoHeight * windowAspect));

         cropX = std::max(0, (mVideoWidth - cropW) / 2);
         cropY = std::max(0, (mVideoHeight - cropH) / 2);
      }
      else
      {
         const DesktopBounds bounds = GetDesktopBounds();
         const float screenW = std::max(1.0f, bounds.maxX - bounds.minX);
         const float screenH = std::max(1.0f, bounds.maxY - bounds.minY);
         const float left = std::clamp((mWindowX - bounds.minX) / screenW, 0.0f, 1.0f);
         const float top = std::clamp((mWindowY - bounds.minY) / screenH, 0.0f, 1.0f);
         const float right = std::clamp((mWindowX + (float)mWindowWidth - bounds.minX) / screenW, 0.0f, 1.0f);
         const float bottom = std::clamp((mWindowY + (float)mWindowHeight - bounds.minY) / screenH, 0.0f, 1.0f);

         cropX = std::clamp((int)std::round(left * (float)mVideoWidth), 0, std::max(0, mVideoWidth - 1));
         cropY = std::clamp((int)std::round(top * (float)mVideoHeight), 0, std::max(0, mVideoHeight - 1));
         cropW = std::clamp((int)std::round((right - left) * (float)mVideoWidth), 1, std::max(1, mVideoWidth - cropX));
         cropH = std::clamp((int)std::round((bottom - top) * (float)mVideoHeight), 1, std::max(1, mVideoHeight - cropY));
      }

      filter = "crop=" + ofToString(cropW) + ":" + ofToString(cropH) + ":" + ofToString(cropX) + ":" + ofToString(cropY);
   }
   else if (mCropMode == (int)CropMode::Manual)
   {
      if (mVideoWidth <= 0 || mVideoHeight <= 0)
         return;

      const int cropX = std::clamp((int)std::round(mCropX), 0, std::max(0, mVideoWidth - 1));
      const int cropY = std::clamp((int)std::round(mCropY), 0, std::max(0, mVideoHeight - 1));
      const int fallbackW = std::max(1, mVideoWidth - cropX);
      const int fallbackH = std::max(1, mVideoHeight - cropY);
      const int cropW = std::clamp((int)std::round(mCropW > 0.0f ? mCropW : (float)fallbackW), 1, fallbackW);
      const int cropH = std::clamp((int)std::round(mCropH > 0.0f ? mCropH : (float)fallbackH), 1, fallbackH);
      filter = "crop=" + ofToString(cropW) + ":" + ofToString(cropH) + ":" + ofToString(cropX) + ":" + ofToString(cropY);
   }

   if (filter == mLastCropFilter)
   {
      mCropNeedsApply = false;
      return;
   }

   if (!mLastCropFilter.empty())
      SendIpcCommand("{\"command\":[\"vf\",\"remove\",\"@bespoke_crop\"]}\n");

   bool ok = true;
   if (!filter.empty())
      ok = SendIpcCommand("{\"command\":[\"vf\",\"add\",\"@bespoke_crop:" + filter + "\"]}\n");

   if (ok)
   {
      mLastCropFilter = filter;
      mCropNeedsApply = false;
   }
}

void MpvPlayer::ApplyWindowPreset(int preset)
{
   const DesktopBounds bounds = GetDesktopBounds();
   const float screenW = std::max(1.0f, bounds.maxX - bounds.minX);
   const float screenH = std::max(1.0f, bounds.maxY - bounds.minY);

   if (preset == 0)
   {
      mWindowWidth = std::max(160, (int)std::round(screenW * 0.5f));
      mWindowHeight = std::max(90, (int)std::round(screenH * 0.5f));
      mWindowX = bounds.minX + (screenW - (float)mWindowWidth) * 0.5f;
      mWindowY = bounds.minY + (screenH - (float)mWindowHeight) * 0.5f;
   }
   else if (preset == 1)
   {
      mWindowX = bounds.minX;
      mWindowY = bounds.minY;
      mWindowWidth = std::max(160, (int)std::round(screenW));
      mWindowHeight = std::max(90, (int)std::round(screenH));
   }
   else if (preset == 2)
   {
      static int sGridSlot = 0;
      const int slot = sGridSlot++ % 4;
      mWindowWidth = std::max(160, (int)std::round(screenW * 0.5f));
      mWindowHeight = std::max(90, (int)std::round(screenH * 0.5f));
      mWindowX = bounds.minX + (slot % 2) * (float)mWindowWidth;
      mWindowY = bounds.minY + (slot / 2) * (float)mWindowHeight;
   }
   else if (preset == 3)
   {
      static int sStripSlot = 0;
      const int slots = 4;
      const int slot = sStripSlot++ % slots;
      mWindowWidth = std::max(160, (int)std::round(screenW / (float)slots));
      mWindowHeight = std::max(90, (int)std::round(screenH));
      mWindowX = bounds.minX + slot * (float)mWindowWidth;
      mWindowY = bounds.minY;
   }
   else
   {
      static int sCascadeSlot = 0;
      const int slot = sCascadeSlot++ % 8;
      mWindowWidth = std::max(160, (int)std::round(screenW * 0.48f));
      mWindowHeight = std::max(90, (int)std::round(screenH * 0.48f));
      mWindowX = bounds.minX + 32.0f * slot;
      mWindowY = bounds.minY + 28.0f * slot;
   }

   SyncGeometryControls();

   mCropNeedsApply = true;
   ApplyWindowGeometry();
   ApplyCropFilter();
}

void MpvPlayer::DuplicateModule()
{
   if (TheSynth == nullptr)
      return;

   const bool sourceWindowIsOpen = mProcess.isRunning();
   ModuleFactory::Spawnable spawnable;
   spawnable.mLabel = "mpvplayer";
   auto* duplicate = dynamic_cast<MpvPlayer*>(TheSynth->SpawnModuleOnTheFly(spawnable, GetPosition(true).x + 24.0f, GetPosition(true).y + 24.0f));
   if (duplicate == nullptr)
   {
      SetStatus("dup failed");
      return;
   }

   CopySettingsTo(*duplicate);
   duplicate->PlaceAwayFromOtherMpvWindows();
   duplicate->SyncGeometryControls();
   duplicate->SetStatus("duplicated");

   if (duplicate->mPlay || sourceWindowIsOpen)
      duplicate->LaunchMpv();
}

void MpvPlayer::CopySettingsTo(MpvPlayer& player) const
{
   player.mMpvExecutable = mMpvExecutable;
   player.mMediaPath = mMediaPath;
   player.mPlay = mPlay;
   player.mMute = mMute;
   player.mVolume = mVolume;
   player.mRate = mRate;
   player.mAnimate = mAnimate;
   player.mAnimationSpeed = mAnimationSpeed;
   player.mOffscreenAmount = mOffscreenAmount;
   player.mCropMode = mCropMode;
   player.mCropX = mCropX;
   player.mCropY = mCropY;
   player.mCropW = mCropW;
   player.mCropH = mCropH;
   player.mTime = mTime;
   player.mTimeSeconds = mTimeSeconds;
   player.mDuration = mDuration;
   player.mDurationControl = mDurationControl;
   player.mVideoWidth = mVideoWidth;
   player.mVideoHeight = mVideoHeight;
   player.mLoopA = mLoopA;
   player.mLoopB = mLoopB;
   player.mLoopAControl = mLoopAControl;
   player.mLoopBControl = mLoopBControl;
   player.mWindowX = mWindowX;
   player.mWindowY = mWindowY;
   player.mWindowWidth = mWindowWidth;
   player.mWindowHeight = mWindowHeight;
   player.mVelocityX = mVelocityX;
   player.mVelocityY = mVelocityY;
   player.mPlaybackPauseNeedsApply = true;
   player.mVolumeNeedsApply = true;
   player.mRateNeedsApply = true;
   player.mTimeNeedsApply = player.mTimeSeconds > 0.0;
   player.mLoopNeedsApply = player.mLoopA >= 0.0 || player.mLoopB >= 0.0;
   player.mCropNeedsApply = true;

   if (player.mMpvEntry != nullptr)
      player.mMpvEntry->SetText(player.mMpvExecutable);
   if (player.mMediaEntry != nullptr)
      player.mMediaEntry->SetText(player.mMediaPath);
   if (player.mPlayCheckbox != nullptr)
      player.mPlayCheckbox->SetValue(player.mPlay ? 1.0f : 0.0f, gTime, false);
   if (player.mMuteCheckbox != nullptr)
      player.mMuteCheckbox->SetValue(player.mMute ? 1.0f : 0.0f, gTime, false);
   if (player.mAnimateCheckbox != nullptr)
      player.mAnimateCheckbox->SetValue(player.mAnimate ? 1.0f : 0.0f, gTime, false);
   if (player.mVolumeSlider != nullptr)
      player.mVolumeSlider->SetValue(player.mVolume, gTime, false);
   if (player.mRateSlider != nullptr)
      player.mRateSlider->SetValue(player.mRate, gTime, false);
   if (player.mAnimationSpeedSlider != nullptr)
      player.mAnimationSpeedSlider->SetValue(player.mAnimationSpeed, gTime, false);
   if (player.mOffscreenSlider != nullptr)
      player.mOffscreenSlider->SetValue(player.mOffscreenAmount, gTime, false);
   if (player.mCropModeDropdown != nullptr)
      player.mCropModeDropdown->SetValue((float)player.mCropMode, gTime, false);
   if (player.mCropXSlider != nullptr)
      player.mCropXSlider->SetValue(player.mCropX, gTime, false);
   if (player.mCropYSlider != nullptr)
      player.mCropYSlider->SetValue(player.mCropY, gTime, false);
   if (player.mCropWSlider != nullptr)
      player.mCropWSlider->SetValue(player.mCropW, gTime, false);
   if (player.mCropHSlider != nullptr)
      player.mCropHSlider->SetValue(player.mCropH, gTime, false);
   if (player.mTimeSlider != nullptr)
      player.mTimeSlider->SetValue(player.mTime, gTime, false);
   if (player.mDurationSlider != nullptr)
      player.mDurationSlider->SetValue(player.mDurationControl, gTime, false);
   if (player.mLoopASlider != nullptr)
      player.mLoopASlider->SetValue(player.mLoopAControl, gTime, false);
   if (player.mLoopBSlider != nullptr)
      player.mLoopBSlider->SetValue(player.mLoopBControl, gTime, false);
}

void MpvPlayer::PlaceAwayFromOtherMpvWindows()
{
   if (TheSynth == nullptr)
      return;

   std::vector<IDrawableModule*> modules;
   TheSynth->GetAllModules(modules);

   std::vector<ofRectangle> occupied;
   for (auto* module : modules)
   {
      auto* player = dynamic_cast<MpvPlayer*>(module);
      if (player == nullptr || player == this)
         continue;
      occupied.emplace_back(player->mWindowX, player->mWindowY, (float)std::max(1, player->mWindowWidth), (float)std::max(1, player->mWindowHeight));
   }

   const DesktopBounds bounds = GetDesktopBounds();
   const float screenW = std::max(1.0f, bounds.maxX - bounds.minX);
   const float screenH = std::max(1.0f, bounds.maxY - bounds.minY);
   const ofVec2f sourceCenter(mWindowX + (float)mWindowWidth * 0.5f, mWindowY + (float)mWindowHeight * 0.5f);

   struct Candidate
   {
      ofRectangle rect;
      float bias{ 0.0f };
   };

   std::vector<Candidate> candidates;
   const int targetCount = std::max(2, (int)occupied.size() + 1);
   const int gridColumns = std::max(1, (int)std::ceil(std::sqrt((float)targetCount)));
   const int gridRows = std::max(1, (int)std::ceil((float)targetCount / (float)gridColumns));
   const float cellW = screenW / (float)gridColumns;
   const float cellH = screenH / (float)gridRows;
   for (int row = 0; row < gridRows; ++row)
   {
      for (int col = 0; col < gridColumns; ++col)
      {
         const float w = std::max(160.0f, cellW);
         const float h = std::max(90.0f, cellH);
         candidates.push_back({ ofRectangle(bounds.minX + col * cellW, bounds.minY + row * cellH, w, h), 0.0f });
      }
   }

   const int stripSlots = std::clamp(targetCount, 2, 8);
   const float stripW = screenW / (float)stripSlots;
   const float stripH = screenH / (float)stripSlots;
   for (int i = 0; i < stripSlots; ++i)
   {
      candidates.push_back({ ofRectangle(bounds.minX + i * stripW, bounds.minY, std::max(160.0f, stripW), screenH), 250.0f });
      candidates.push_back({ ofRectangle(bounds.minX, bounds.minY + i * stripH, screenW, std::max(90.0f, stripH)), 300.0f });
   }

   const float keptW = std::min((float)std::max(160, mWindowWidth), screenW);
   const float keptH = std::min((float)std::max(90, mWindowHeight), screenH);
   for (int i = 0; i < 12; ++i)
   {
      const float x = bounds.minX + std::fmod(mWindowX - bounds.minX + (float)(i + 1) * keptW, std::max(1.0f, screenW - keptW + 1.0f));
      const float y = bounds.minY + std::fmod(mWindowY - bounds.minY + (float)((i / 3) + 1) * 48.0f, std::max(1.0f, screenH - keptH + 1.0f));
      candidates.push_back({ ofRectangle(x, y, keptW, keptH), 500.0f });
   }

   if (candidates.empty())
      return;

   auto best = candidates.front();
   float bestScore = std::numeric_limits<float>::max();
   for (const auto& candidate : candidates)
   {
      float overlap = 0.0f;
      for (const auto& rect : occupied)
         overlap += RectOverlapArea(candidate.rect, rect);

      const ofVec2f center(candidate.rect.getCenter().x, candidate.rect.getCenter().y);
      const float distance = std::hypot(center.x - sourceCenter.x, center.y - sourceCenter.y);
      const float score = overlap * 10000.0f + distance * 0.01f + candidate.bias;
      if (score < bestScore)
      {
         bestScore = score;
         best = candidate;
      }
   }

   mWindowX = best.rect.x;
   mWindowY = best.rect.y;
   mWindowWidth = std::max(160, (int)std::round(best.rect.width));
   mWindowHeight = std::max(90, (int)std::round(best.rect.height));
   mCropNeedsApply = true;
}

void MpvPlayer::SyncGeometryControls()
{
   if (mXSlider)
      mXSlider->SetValue(mWindowX, gTime, false);
   if (mYSlider)
      mYSlider->SetValue(mWindowY, gTime, false);
   if (mWidthSlider)
      mWidthSlider->SetValue((float)mWindowWidth, gTime, false);
   if (mHeightSlider)
      mHeightSlider->SetValue((float)mWindowHeight, gTime, false);
}

void MpvPlayer::PollPlaybackPosition()
{
   if (!mProcess.isRunning())
      return;

   if (gTime - mLastPlaybackPollTime < 1000.0)
      return;

   mLastPlaybackPollTime = gTime;
   if (GetMpvNumberProperty("duration", mDuration))
   {
      mDurationControl = (float)mDuration;
      if (mDurationSlider && !mDurationSlider->IsMouseDown())
      {
         mDurationSlider->SetExtents(0.0f, (float)std::max(1.0, mDuration));
         mDurationSlider->SetValue(mDurationControl, gTime, false);
      }
   }
   if (mTimeSlider)
      mTimeSlider->SetExtents(0.0f, (float)std::max(1.0, mDuration));
   if (mLoopASlider)
      mLoopASlider->SetExtents(-1.0f, (float)std::max(1.0, mDuration));
   if (mLoopBSlider)
      mLoopBSlider->SetExtents(-1.0f, (float)std::max(1.0, mDuration));

   double videoWidth = 0.0;
   double videoHeight = 0.0;
   if (mVideoWidth <= 0 && GetMpvNumberProperty("video-params/w", videoWidth))
      mVideoWidth = std::max(1, (int)std::round(videoWidth));
   if (mVideoHeight <= 0 && GetMpvNumberProperty("video-params/h", videoHeight))
      mVideoHeight = std::max(1, (int)std::round(videoHeight));
   if ((mVideoWidth <= 0 || mVideoHeight <= 0) && GetMpvNumberProperty("width", videoWidth) && GetMpvNumberProperty("height", videoHeight))
   {
      mVideoWidth = std::max(1, (int)std::round(videoWidth));
      mVideoHeight = std::max(1, (int)std::round(videoHeight));
   }
   if (mVideoWidth > 0 && mVideoHeight > 0 && mLastCropFilter.empty() && mCropMode != (int)CropMode::Fit)
      mCropNeedsApply = true;
   if (mVideoWidth > 0 && mVideoHeight > 0)
   {
      if (mCropXSlider)
         mCropXSlider->SetExtents(0.0f, (float)std::max(1, mVideoWidth - 1));
      if (mCropYSlider)
         mCropYSlider->SetExtents(0.0f, (float)std::max(1, mVideoHeight - 1));
      if (mCropWSlider)
         mCropWSlider->SetExtents(0.0f, (float)mVideoWidth);
      if (mCropHSlider)
         mCropHSlider->SetExtents(0.0f, (float)mVideoHeight);
   }

   bool mute = mMute;
   if (GetMpvBoolProperty("mute", mute) && mute != mMute)
   {
      mMute = mute;
      if (mMuteCheckbox != nullptr)
         mMuteCheckbox->SetValue(mMute ? 1.0f : 0.0f, gTime, false);
   }

   double volume = mVolume;
   if (!mVolumeNeedsApply && GetMpvNumberProperty("volume", volume) && std::abs(volume - mVolume) > 0.1)
   {
      mVolume = (float)std::clamp(volume, 0.0, 130.0);
      if (mVolumeSlider && !mVolumeSlider->IsMouseDown())
         mVolumeSlider->SetValue(mVolume, gTime, false);
   }

   double rate = mRate;
   if (!mRateNeedsApply && GetMpvNumberProperty("speed", rate) && std::abs(rate - mRate) > 0.001)
   {
      mRate = (float)std::clamp(rate, 0.1, 4.0);
      if (mRateSlider && !mRateSlider->IsMouseDown())
         mRateSlider->SetValue(mRate, gTime, false);
   }

   double timeSeconds = 0.0;
   if (GetMpvNumberProperty("time-pos", timeSeconds))
   {
      if (mWaitingForTimeSeekAck)
      {
         const bool reachedTarget = std::abs(timeSeconds - mTimeSeekTarget) <= 1.0 || timeSeconds >= mTimeSeekTarget;
         const bool timedOut = gTime - mTimeSeekStartTime > 3000.0;
         if (!reachedTarget && !timedOut)
         {
            mTimeSeconds = mTimeSeekTarget;
            if (mTimeSlider && !mTimeSlider->IsMouseDown())
            {
               mTime = (float)std::clamp(mTimeSeconds, 0.0, std::max(1.0, mDuration));
               mTimeSlider->SetValue(mTime, gTime, false);
            }
            return;
         }

         mWaitingForTimeSeekAck = false;
         if (timedOut && !reachedTarget)
         {
            mTimeNeedsApply = true;
            return;
         }
      }

      mTimeSeconds = timeSeconds;
      if (mTimeSlider && !mTimeSlider->IsMouseDown())
      {
         mTime = (float)std::clamp(mTimeSeconds, 0.0, std::max(1.0, mDuration));
         mTimeSlider->SetValue(mTime, gTime, false);
      }
   }

   if (mDuration > 0.0)
      SetStatus(std::string(mPlay ? "playing " : "paused ") + FormatTime(mTimeSeconds) + " / " + FormatTime(mDuration));
}

void MpvPlayer::ScrubToTime()
{
   if (!mProcess.isRunning())
      return;

   if (mDuration > 0.0)
   {
      mTimeSeconds = std::clamp((double)mTime, 0.0, mDuration);
      mTimeNeedsApply = true;
      ApplyTimeSeek();
   }
   else
   {
      SendIpcCommand("{\"command\":[\"seek\"," + ofToString(std::clamp(mTime, 0.0f, 1.0f) * 100.0f) + ",\"absolute-percent\"]}\n");
   }
}

void MpvPlayer::SetLoopPoint(bool start)
{
   if (!mProcess.isRunning())
      return;

   double timeSeconds = mTimeSeconds;
   if (GetMpvNumberProperty("time-pos", timeSeconds))
   {
      mTimeSeconds = timeSeconds;
      if (mTimeSlider && !mTimeSlider->IsMouseDown())
      {
         mTime = (float)std::clamp(mTimeSeconds, 0.0, std::max(1.0, mDuration));
         mTimeSlider->SetValue(mTime, gTime, false);
      }
   }

   if (start)
   {
      mLoopA = timeSeconds;
      mLoopAControl = (float)mLoopA;
      mLoopNeedsApply = true;
      if (mLoopASlider && !mLoopASlider->IsMouseDown())
         mLoopASlider->SetValue((float)mLoopA, gTime, false);
      ApplyLoopPoints();
   }
   else
   {
      mLoopB = timeSeconds;
      mLoopBControl = (float)mLoopB;
      mLoopNeedsApply = true;
      if (mLoopBSlider && !mLoopBSlider->IsMouseDown())
         mLoopBSlider->SetValue((float)mLoopB, gTime, false);
      ApplyLoopPoints();
   }
}

void MpvPlayer::ClearLoop()
{
   mLoopA = -1.0;
   mLoopB = -1.0;
   mLoopAControl = (float)mLoopA;
   mLoopBControl = (float)mLoopB;
   mLoopNeedsApply = false;
   if (mLoopASlider)
      mLoopASlider->SetValue((float)mLoopA, gTime, false);
   if (mLoopBSlider)
      mLoopBSlider->SetValue((float)mLoopB, gTime, false);
   SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-a\",\"no\"]}\n");
   SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-b\",\"no\"]}\n");
}

void MpvPlayer::ApplyWindowGeometry()
{
   if (!mProcess.isRunning())
      return;

   const WindowGeometry geometry = GetNativeWindowGeometry(mWindowX, mWindowY, mWindowWidth, mWindowHeight);
   const bool changed = mLastAppliedWindowX != mWindowX ||
                        mLastAppliedWindowY != mWindowY ||
                        mLastAppliedWindowWidth != mWindowWidth ||
                        mLastAppliedWindowHeight != mWindowHeight;
   if (!changed || gTime - mLastGeometryApplyTime < 50.0)
      return;

#if BESPOKE_WINDOWS
   const juce::String title(GetWindowTitle());
   HWND hwnd = FindWindowW(nullptr, title.toWideCharPointer());
   if (hwnd == nullptr)
      return;

   SetWindowPos(hwnd, HWND_TOPMOST, geometry.x, geometry.y, geometry.width, geometry.height, SWP_NOACTIVATE);
#elif JUCE_MAC
   const juce::String title = ToAppleScriptString(GetWindowTitle());
   juce::StringArray args;
   args.add("osascript");
   args.add("-e");
   args.add("tell application \"System Events\" to tell process \"mpv\" to set position of first window whose name is " + title + " to {" + juce::String(geometry.x) + ", " + juce::String(geometry.y) + "}");
   args.add("-e");
   args.add("tell application \"System Events\" to tell process \"mpv\" to set size of first window whose name is " + title + " to {" + juce::String(geometry.width) + ", " + juce::String(geometry.height) + "}");
   RunShortProcess(args);
#elif JUCE_LINUX || JUCE_BSD
   const juce::String title(GetWindowTitle());
   juce::StringArray aboveArgs;
   aboveArgs.add("wmctrl");
   aboveArgs.add("-r");
   aboveArgs.add(title);
   aboveArgs.add("-b");
   aboveArgs.add("add,above");
   RunShortProcess(aboveArgs, 100);

   juce::StringArray moveArgs;
   moveArgs.add("wmctrl");
   moveArgs.add("-r");
   moveArgs.add(title);
   moveArgs.add("-e");
   moveArgs.add("0," + juce::String(geometry.x) + "," + juce::String(geometry.y) + "," + juce::String(geometry.width) + "," + juce::String(geometry.height));
   RunShortProcess(moveArgs, 100);
#endif

   mLastGeometryApplyTime = gTime;
   mLastAppliedWindowX = mWindowX;
   mLastAppliedWindowY = mWindowY;
   mLastAppliedWindowWidth = mWindowWidth;
   mLastAppliedWindowHeight = mWindowHeight;
}

void MpvPlayer::SyncWindowGeometryFromNative()
{
   if (!mProcess.isRunning())
      return;

   if (!std::isfinite(mLastAppliedWindowX) || !std::isfinite(mLastAppliedWindowY) || mLastAppliedWindowWidth < 1 || mLastAppliedWindowHeight < 1)
      return;

   if (gTime - mLastNativeGeometryPollTime < 250.0)
      return;
   mLastNativeGeometryPollTime = gTime;

#if BESPOKE_WINDOWS
   const juce::String title(GetWindowTitle());
   HWND hwnd = FindWindowW(nullptr, title.toWideCharPointer());
   if (hwnd == nullptr)
      return;

   RECT rect{};
   if (!GetWindowRect(hwnd, &rect))
      return;

   const juce::Rectangle<int> nativeRect(rect.left, rect.top, std::max(1L, rect.right - rect.left), std::max(1L, rect.bottom - rect.top));
   const juce::Rectangle<int> logicalRect = juce::Desktop::getInstance().getDisplays().physicalToLogical(nativeRect);
   if (logicalRect.isEmpty())
      return;

   const float nativeX = (float)logicalRect.getX();
   const float nativeY = (float)logicalRect.getY();
   const int nativeWidth = std::max(1, logicalRect.getWidth());
   const int nativeHeight = std::max(1, logicalRect.getHeight());
   const bool movedExternally =
      std::abs(nativeX - mLastAppliedWindowX) > 2.0f ||
      std::abs(nativeY - mLastAppliedWindowY) > 2.0f ||
      std::abs(nativeWidth - mLastAppliedWindowWidth) > 2 ||
      std::abs(nativeHeight - mLastAppliedWindowHeight) > 2;

   if (!movedExternally)
      return;

   mWindowX = nativeX;
   mWindowY = nativeY;
   mWindowWidth = nativeWidth;
   mWindowHeight = nativeHeight;
   mLastAppliedWindowX = mWindowX;
   mLastAppliedWindowY = mWindowY;
   mLastAppliedWindowWidth = mWindowWidth;
   mLastAppliedWindowHeight = mWindowHeight;
   mLastGeometryApplyTime = gTime;
   mCropNeedsApply = true;

   if (mXSlider && !mXSlider->IsMouseDown())
      mXSlider->SetValue(mWindowX, gTime, false);
   if (mYSlider && !mYSlider->IsMouseDown())
      mYSlider->SetValue(mWindowY, gTime, false);
   if (mWidthSlider && !mWidthSlider->IsMouseDown())
      mWidthSlider->SetValue((float)mWindowWidth, gTime, false);
   if (mHeightSlider && !mHeightSlider->IsMouseDown())
      mHeightSlider->SetValue((float)mWindowHeight, gTime, false);
#endif
}

void MpvPlayer::UpdateAnimation()
{
   if (!mAnimate || !mProcess.isRunning())
   {
      mLastAnimationTime = gTime;
      return;
   }

   const double now = gTime;
   if (mLastAnimationTime < 0)
      mLastAnimationTime = now;

   const float step = std::clamp((float)((now - mLastAnimationTime) / 16.6667), 0.0f, 4.0f);
   mLastAnimationTime = now;

   mWindowX += mVelocityX * step * mAnimationSpeed;
   mWindowY += mVelocityY * step * mAnimationSpeed;
   const DesktopBounds bounds = GetDesktopBounds();
   const float offscreen = std::clamp(mOffscreenAmount, 0.0f, 1.0f);
   const float minX = bounds.minX - (float)mWindowWidth * offscreen;
   const float minY = bounds.minY - (float)mWindowHeight * offscreen;
   const float maxX = std::max(minX, bounds.maxX - (float)mWindowWidth + (float)mWindowWidth * offscreen);
   const float maxY = std::max(minY, bounds.maxY - (float)mWindowHeight + (float)mWindowHeight * offscreen);

   const bool hitX = mWindowX <= minX || mWindowX >= maxX;
   const bool hitY = mWindowY <= minY || mWindowY >= maxY;

   if (hitX)
      mVelocityX = -mVelocityX;
   if (hitY)
      mVelocityY = -mVelocityY;
   if (hitX || hitY)
      JitterAnimationVelocity(hitX, hitY);

   mWindowX = std::clamp(mWindowX, minX, maxX);
   mWindowY = std::clamp(mWindowY, minY, maxY);
   mCropNeedsApply = true;

   if (mXSlider)
      mXSlider->SetValue(mWindowX, gTime, false);
   if (mYSlider)
      mYSlider->SetValue(mWindowY, gTime, false);
}

void MpvPlayer::RandomizeAnimationVelocity()
{
   constexpr float kMinVelocity = 3.0f;
   constexpr float kMaxVelocity = 7.0f;
   constexpr float kTwoPi = 6.2831853f;
   const float angle = juce::Random::getSystemRandom().nextFloat() * kTwoPi;
   const float magnitude = kMinVelocity + juce::Random::getSystemRandom().nextFloat() * (kMaxVelocity - kMinVelocity);

   mVelocityX = std::cos(angle) * magnitude;
   mVelocityY = std::sin(angle) * magnitude;

   if (std::abs(mVelocityX) < 1.0f)
      mVelocityX = std::copysign(1.0f, mVelocityX == 0.0f ? 1.0f : mVelocityX);
   if (std::abs(mVelocityY) < 1.0f)
      mVelocityY = std::copysign(1.0f, mVelocityY == 0.0f ? 1.0f : mVelocityY);
}

void MpvPlayer::JitterAnimationVelocity(bool hitX, bool hitY)
{
   constexpr float kMinVelocity = 3.0f;
   constexpr float kMaxVelocity = 7.0f;
   const float currentMagnitude = std::hypot(mVelocityX, mVelocityY);
   const float magnitude = std::clamp(currentMagnitude * (0.9f + juce::Random::getSystemRandom().nextFloat() * 0.2f), kMinVelocity, kMaxVelocity);
   const float angleJitter = (juce::Random::getSystemRandom().nextFloat() - 0.5f) * 0.7f;
   const float angle = std::atan2(mVelocityY, mVelocityX) + angleJitter;
   const float signX = mVelocityX < 0.0f ? -1.0f : 1.0f;
   const float signY = mVelocityY < 0.0f ? -1.0f : 1.0f;

   mVelocityX = std::cos(angle) * magnitude;
   mVelocityY = std::sin(angle) * magnitude;

   if (hitX)
      mVelocityX = std::abs(mVelocityX) * signX;
   if (hitY)
      mVelocityY = std::abs(mVelocityY) * signY;
   if (std::abs(mVelocityX) < 1.0f)
      mVelocityX = signX;
   if (std::abs(mVelocityY) < 1.0f)
      mVelocityY = signY;
}

std::string MpvPlayer::GetWindowTitle() const
{
   return "bespoke_mpv_" + ofToString((uintptr_t)this);
}

std::string MpvPlayer::GetIpcServerName() const
{
#if BESPOKE_WINDOWS
   return "\\\\.\\pipe\\" + GetWindowTitle();
#else
   return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(GetWindowTitle() + ".sock").getFullPathName().toStdString();
#endif
}

bool MpvPlayer::SendIpcCommand(const std::string& command, std::string* response)
{
#if BESPOKE_WINDOWS
   const juce::String pipeName(GetIpcServerName());
   for (int i = 0; i < 5; ++i)
   {
      HANDLE pipe = CreateFileW(pipeName.toWideCharPointer(), response ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (pipe != INVALID_HANDLE_VALUE)
      {
         DWORD bytesWritten = 0;
         const BOOL ok = WriteFile(pipe, command.data(), (DWORD)command.size(), &bytesWritten, nullptr);
         if (ok && response)
         {
            char buffer[4096];
            DWORD bytesRead = 0;
            if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr))
            {
               buffer[bytesRead] = '\0';
               *response = buffer;
            }
         }
         CloseHandle(pipe);
         return ok && bytesWritten == command.size();
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
   }
#elif JUCE_MAC || JUCE_LINUX || JUCE_BSD
   const std::string socketPath = GetIpcServerName();
   if (socketPath.size() >= sizeof(sockaddr_un::sun_path))
      return false;

   for (int i = 0; i < 5; ++i)
   {
      const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
      if (fd < 0)
         return false;

      sockaddr_un addr;
      std::memset(&addr, 0, sizeof(addr));
      addr.sun_family = AF_UNIX;
      std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

      if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0)
      {
         const ssize_t bytesWritten = write(fd, command.data(), command.size());
         if (bytesWritten == (ssize_t)command.size() && response)
         {
            char buffer[4096];
            const ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0)
            {
               buffer[bytesRead] = '\0';
               *response = buffer;
            }
         }
         close(fd);
         return bytesWritten == (ssize_t)command.size();
      }

      close(fd);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
   }
#endif

   return false;
}

bool MpvPlayer::GetMpvNumberProperty(const std::string& property, double& value)
{
   std::string response;
   if (!SendIpcCommand("{\"command\":[\"get_property\",\"" + property + "\"]}\n", &response))
      return false;

   ofxJSONElement root;
   if (!root.parse(response))
      return false;

   if (!root["data"].isNumeric())
      return false;

   value = root["data"].asDouble();
   return true;
}

bool MpvPlayer::GetMpvBoolProperty(const std::string& property, bool& value)
{
   std::string response;
   if (!SendIpcCommand("{\"command\":[\"get_property\",\"" + property + "\"]}\n", &response))
      return false;

   ofxJSONElement root;
   if (!root.parse(response))
      return false;

   if (!root["data"].isBool())
      return false;

   value = root["data"].asBool();
   return true;
}

std::string MpvPlayer::FormatTime(double seconds) const
{
   if (seconds < 0.0 || !std::isfinite(seconds))
      return "--:--";

   const int wholeSeconds = (int)std::floor(seconds);
   const int minutes = wholeSeconds / 60;
   const int remainingSeconds = wholeSeconds % 60;
   const int milliseconds = std::clamp((int)std::floor((seconds - wholeSeconds) * 1000.0), 0, 999);
   return ofToString(minutes) + ":" +
          (remainingSeconds < 10 ? "0" : "") + ofToString(remainingSeconds) + "." +
          (milliseconds < 100 ? "0" : "") + (milliseconds < 10 ? "0" : "") + ofToString(milliseconds);
}

std::string MpvPlayer::ResolveMediaPath(const std::string& path) const
{
   if (path.empty() || juce::File::isAbsolutePath(path) || juce::URL::isProbablyAWebsiteURL(path))
      return path;

   const juce::File workingDirPath = juce::File::getCurrentWorkingDirectory().getChildFile(path);
   if (workingDirPath.exists())
      return workingDirPath.getFullPathName().toStdString();

   const std::string dataPath = ofToDataPath(path);
   if (juce::File(dataPath).exists())
      return dataPath;

   return ofToResourcePath(path);
}

void MpvPlayer::SetStatus(const std::string& status)
{
   mStatus = status;
}

void MpvPlayer::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("mpv", moduleInfo, "mpv");
   mModuleSaveData.LoadString("media", moduleInfo, "");
   mModuleSaveData.LoadFloat("x", moduleInfo, 100.0f, -4000.0f, 4000.0f);
   mModuleSaveData.LoadFloat("y", moduleInfo, 100.0f, -4000.0f, 4000.0f);
   mModuleSaveData.LoadInt("w", moduleInfo, 640, 1, 4096);
   mModuleSaveData.LoadInt("h", moduleInfo, 360, 1, 4096);
   mModuleSaveData.LoadFloat("animation_speed", moduleInfo, 1.0f, 0.01f, 1.0f);
   mModuleSaveData.LoadFloat("offscreen", moduleInfo, 0.5f, 0.0f, 1.0f);
   mModuleSaveData.LoadFloat("volume", moduleInfo, 100.0f, 0.0f, 130.0f);
   mModuleSaveData.LoadFloat("rate", moduleInfo, 1.0f, 0.1f, 4.0f);
   mModuleSaveData.LoadInt("crop_mode", moduleInfo, (int)CropMode::Fit, (int)CropMode::Fit, (int)CropMode::Manual);
   mModuleSaveData.LoadFloat("crop_x", moduleInfo, 0.0f, 0.0f, 4096.0f);
   mModuleSaveData.LoadFloat("crop_y", moduleInfo, 0.0f, 0.0f, 4096.0f);
   mModuleSaveData.LoadFloat("crop_w", moduleInfo, 0.0f, 0.0f, 4096.0f);
   mModuleSaveData.LoadFloat("crop_h", moduleInfo, 0.0f, 0.0f, 4096.0f);

   mPlay = !moduleInfo["play"].isNull() && moduleInfo["play"].asBool();
   mMute = !moduleInfo["mute"].isNull() && moduleInfo["mute"].asBool();
   mAnimate = !moduleInfo["animate"].isNull() && moduleInfo["animate"].asBool();
   mTimeSeconds = !moduleInfo["time"].isNull() ? moduleInfo["time"].asDouble() : 0.0;
   mTime = (float)mTimeSeconds;
   mTimeNeedsApply = mTimeSeconds > 0.0;
   mLoopA = !moduleInfo["loop_a"].isNull() ? moduleInfo["loop_a"].asDouble() : -1.0;
   mLoopB = !moduleInfo["loop_b"].isNull() ? moduleInfo["loop_b"].asDouble() : -1.0;
   mLoopAControl = (float)mLoopA;
   mLoopBControl = (float)mLoopB;
   mLoopNeedsApply = mLoopA >= 0.0 || mLoopB >= 0.0;
   mVolumeNeedsApply = true;
   mRateNeedsApply = true;
   mCropNeedsApply = true;

   SetUpFromSaveData();
}

void MpvPlayer::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["mpv"] = mMpvExecutable;
   moduleInfo["media"] = mMediaPath;
   moduleInfo["play"] = mPlay;
   moduleInfo["mute"] = mMute;
   moduleInfo["animate"] = mAnimate;
   moduleInfo["animation_speed"] = mAnimationSpeed;
   moduleInfo["offscreen"] = mOffscreenAmount;
   moduleInfo["volume"] = mVolume;
   moduleInfo["rate"] = mRate;
   moduleInfo["crop_mode"] = mCropMode;
   moduleInfo["crop_x"] = mCropX;
   moduleInfo["crop_y"] = mCropY;
   moduleInfo["crop_w"] = mCropW;
   moduleInfo["crop_h"] = mCropH;
   moduleInfo["time"] = mTimeSeconds;
   moduleInfo["loop_a"] = mLoopA;
   moduleInfo["loop_b"] = mLoopB;
   moduleInfo["x"] = mWindowX;
   moduleInfo["y"] = mWindowY;
   moduleInfo["w"] = mWindowWidth;
   moduleInfo["h"] = mWindowHeight;
}

void MpvPlayer::SetUpFromSaveData()
{
   mMpvExecutable = mModuleSaveData.GetString("mpv");
   mMediaPath = mModuleSaveData.GetString("media");
   mWindowX = mModuleSaveData.GetFloat("x");
   mWindowY = mModuleSaveData.GetFloat("y");
   mWindowWidth = mModuleSaveData.GetInt("w");
   mWindowHeight = mModuleSaveData.GetInt("h");
   mAnimationSpeed = mModuleSaveData.GetFloat("animation_speed");
   mAnimationSpeed = std::clamp(mAnimationSpeed, 0.01f, 1.0f);
   mOffscreenAmount = mModuleSaveData.GetFloat("offscreen");
   mVolume = mModuleSaveData.GetFloat("volume");
   mRate = mModuleSaveData.GetFloat("rate");
   mCropMode = mModuleSaveData.GetInt("crop_mode");
   mCropX = mModuleSaveData.GetFloat("crop_x");
   mCropY = mModuleSaveData.GetFloat("crop_y");
   mCropW = mModuleSaveData.GetFloat("crop_w");
   mCropH = mModuleSaveData.GetFloat("crop_h");
   mVolumeNeedsApply = true;
   mRateNeedsApply = true;
   mCropNeedsApply = true;
}

void MpvPlayer::SaveState(FileStreamOut& out)
{
   out << GetModuleSaveStateRev();
   IDrawableModule::SaveState(out);
   out << mMpvExecutable;
   out << mMediaPath;
   out << mPlay;
   out << mMute;
   out << mAnimate;
   out << mAnimationSpeed;
   out << mOffscreenAmount;
   out << mTimeSeconds;
   out << mLoopA;
   out << mLoopB;
   out << mWindowX;
   out << mWindowY;
   out << mWindowWidth;
   out << mWindowHeight;
   out << mCropMode;
   out << mVolume;
   out << mRate;
   out << mCropX;
   out << mCropY;
   out << mCropW;
   out << mCropH;
}

void MpvPlayer::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   LoadStateValidate(rev <= GetModuleSaveStateRev());
   if (rev >= 1)
   {
      in >> mMpvExecutable;
      in >> mMediaPath;
      in >> mPlay;
      if (rev >= 2)
         in >> mMute;
      in >> mAnimate;
      if (rev >= 4)
         in >> mAnimationSpeed;
      mAnimationSpeed = std::clamp(mAnimationSpeed, 0.01f, 1.0f);
      if (rev >= 5)
         in >> mOffscreenAmount;
      if (rev >= 6)
      {
         in >> mTimeSeconds;
         mTime = (float)mTimeSeconds;
         mTimeNeedsApply = mTimeSeconds > 0.0;
      }
      else
      {
         mTimeSeconds = mTime;
         mTimeNeedsApply = mTimeSeconds > 0.0;
      }
      if (rev >= 3)
      {
         in >> mLoopA;
         in >> mLoopB;
         mLoopAControl = (float)mLoopA;
         mLoopBControl = (float)mLoopB;
         mLoopNeedsApply = mLoopA >= 0.0 || mLoopB >= 0.0;
      }
      in >> mWindowX;
      in >> mWindowY;
      in >> mWindowWidth;
      in >> mWindowHeight;
      if (rev >= 7)
         in >> mCropMode;
      if (rev >= 8)
      {
         in >> mVolume;
         in >> mRate;
         in >> mCropX;
         in >> mCropY;
         in >> mCropW;
         in >> mCropH;
      }
      mCropMode = std::clamp(mCropMode, (int)CropMode::Fit, (int)CropMode::Manual);
      mVolume = std::clamp(mVolume, 0.0f, 130.0f);
      mRate = std::clamp(mRate, 0.1f, 4.0f);
      mVolumeNeedsApply = true;
      mRateNeedsApply = true;
      mCropNeedsApply = true;
   }
}
