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
{
}

MpvPlayer::~MpvPlayer()
{
   StopMpv();
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
   BUTTON(mRestartButton, "restart");
   UIBLOCK_NEWLINE();
   FLOATSLIDER_DIGITS(mTimeSlider, "time", &mTime, 0, 1, 3);
   UIBLOCK_NEWLINE();
   BUTTON(mSetLoopAButton, "set a");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mSetLoopBButton, "set b");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mClearLoopButton, "clear ab");
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
   FLOATSLIDER_DIGITS(mAnimationSpeedSlider, "speed", &mAnimationSpeed, 0.1f, 4.0f, 2);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER_DIGITS(mOffscreenSlider, "offscreen", &mOffscreenAmount, 0.0f, 1.0f, 2);
   UIBLOCK_POPSLIDERWIDTH();
   ENDUIBLOCK(mWidth, mHeight);

   mHeight += 20;
}

void MpvPlayer::Poll()
{
   if (mPlay && !mProcess.isRunning())
      LaunchMpv();

   if (!mPlay && mProcess.isRunning())
      StopMpv();

   SyncWindowGeometryFromNative();
   UpdateAnimation();
   ApplyWindowGeometry();
   PollPlaybackPosition();
}

void MpvPlayer::OpenMedia(const std::string& mediaPath, bool play)
{
   mMediaPath = mediaPath;
   mPlay = play;
   if (mMediaEntry != nullptr)
      mMediaEntry->SetText(mMediaPath);
   if (mPlayCheckbox != nullptr)
      mPlayCheckbox->SetValue(mPlay ? 1.0f : 0.0f, gTime, false);

   if (mPlay)
      LaunchMpv();
}

void MpvPlayer::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mMpvEntry->Draw();
   mMediaEntry->Draw();
   mPlayCheckbox->Draw();
   mMuteCheckbox->Draw();
   mRestartButton->Draw();
   mTimeSlider->Draw();
   mSetLoopAButton->Draw();
   mSetLoopBButton->Draw();
   mClearLoopButton->Draw();
   mXSlider->Draw();
   mYSlider->Draw();
   mWidthSlider->Draw();
   mHeightSlider->Draw();
   mAnimateCheckbox->Draw();
   mAnimationSpeedSlider->Draw();
   mOffscreenSlider->Draw();
   DrawTextNormal(mStatus, 3, mHeight - 5);
}

void MpvPlayer::ButtonClicked(ClickButton* button, double)
{
   if (button == mRestartButton)
      LaunchMpv();
   if (button == mSetLoopAButton)
      SetLoopPoint(true);
   if (button == mSetLoopBButton)
      SetLoopPoint(false);
   if (button == mClearLoopButton)
      ClearLoop();
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
   else if (slider == mAnimationSpeedSlider || slider == mOffscreenSlider)
      return;
   else
      ApplyWindowGeometry();
}

void MpvPlayer::IntSliderUpdated(IntSlider*, int, double)
{
   ApplyWindowGeometry();
}

void MpvPlayer::CheckboxUpdated(Checkbox* checkbox, double)
{
   if (checkbox == mPlayCheckbox)
   {
      if (mPlay)
         LaunchMpv();
      else
         StopMpv();
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
   if (mLoopA >= 0.0)
      SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-a\"," + FormatMpvNumber(mLoopA) + "]}\n");
   if (mLoopB >= 0.0)
      SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-b\"," + FormatMpvNumber(mLoopB) + "]}\n");
}

void MpvPlayer::StopMpv()
{
   if (mProcess.isRunning())
      mProcess.kill();
}

juce::StringArray MpvPlayer::BuildMpvArgs() const
{
   juce::StringArray args;
   args.add(mMpvExecutable);
   args.add("--force-window=yes");
   args.add("--ontop=yes");
   args.add("--no-terminal");
   args.add("--input-ipc-server=" + juce::String(GetIpcServerName()));
   args.add(mMute ? "--mute=yes" : "--mute=no");
   args.add("--title=" + juce::String(GetWindowTitle()));
   const WindowGeometry geometry = GetNativeWindowGeometry(mWindowX, mWindowY, mWindowWidth, mWindowHeight);
   args.add("--geometry=" + juce::String(geometry.width) + "x" + juce::String(geometry.height) + "+" + juce::String(geometry.x) + "+" + juce::String(geometry.y));
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

void MpvPlayer::PollPlaybackPosition()
{
   if (!mProcess.isRunning())
      return;

   if (gTime - mLastPlaybackPollTime < 250.0)
      return;

   mLastPlaybackPollTime = gTime;
   GetMpvNumberProperty("duration", mDuration);
   if (mTimeSlider)
      mTimeSlider->SetExtents(0.0f, (float)std::max(1.0, mDuration));

   double timeSeconds = 0.0;
   if (GetMpvNumberProperty("time-pos", timeSeconds))
   {
      mTimeSeconds = timeSeconds;
      if (mTimeSlider && !mTimeSlider->IsMouseDown())
      {
         mTime = (float)std::clamp(mTimeSeconds, 0.0, std::max(1.0, mDuration));
         mTimeSlider->SetValue(mTime, gTime, false);
      }
   }

   if (mDuration > 0.0)
      SetStatus("playing " + FormatTime(mTimeSeconds) + " / " + FormatTime(mDuration));
}

void MpvPlayer::ScrubToTime()
{
   if (!mProcess.isRunning())
      return;

   if (mDuration > 0.0)
   {
      mTimeSeconds = std::clamp((double)mTime, 0.0, mDuration);
      SendIpcCommand("{\"command\":[\"set_property\",\"time-pos\"," + FormatMpvNumber(mTimeSeconds) + "]}\n");
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
      SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-a\"," + FormatMpvNumber(mLoopA) + "]}\n");
   }
   else
   {
      mLoopB = timeSeconds;
      SendIpcCommand("{\"command\":[\"set_property\",\"ab-loop-b\"," + FormatMpvNumber(mLoopB) + "]}\n");
   }
}

void MpvPlayer::ClearLoop()
{
   mLoopA = -1.0;
   mLoopB = -1.0;
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

   if (gTime - mLastNativeGeometryPollTime < 80.0)
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
   mModuleSaveData.LoadFloat("animation_speed", moduleInfo, 1.0f, 0.1f, 4.0f);
   mModuleSaveData.LoadFloat("offscreen", moduleInfo, 0.0f, 0.0f, 1.0f);

   mPlay = !moduleInfo["play"].isNull() && moduleInfo["play"].asBool();
   mMute = !moduleInfo["mute"].isNull() && moduleInfo["mute"].asBool();
   mAnimate = !moduleInfo["animate"].isNull() && moduleInfo["animate"].asBool();
   mLoopA = !moduleInfo["loop_a"].isNull() ? moduleInfo["loop_a"].asDouble() : -1.0;
   mLoopB = !moduleInfo["loop_b"].isNull() ? moduleInfo["loop_b"].asDouble() : -1.0;

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
   mOffscreenAmount = mModuleSaveData.GetFloat("offscreen");
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
   out << mLoopA;
   out << mLoopB;
   out << mWindowX;
   out << mWindowY;
   out << mWindowWidth;
   out << mWindowHeight;
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
      if (rev >= 5)
         in >> mOffscreenAmount;
      if (rev >= 3)
      {
         in >> mLoopA;
         in >> mLoopB;
      }
      in >> mWindowX;
      in >> mWindowY;
      in >> mWindowWidth;
      in >> mWindowHeight;
   }
}
