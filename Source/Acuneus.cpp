/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2021 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#include "Acuneus.h"

#include "FileStream.h"
#include "ModularSynth.h"
#include "SynthGlobals.h"
#include "Transport.h"
#include "UIControlMacros.h"
#include "UserPrefs.h"
#include "ofxJSONElement.h"

#if BESPOKE_ACUNEUS_ENABLED
#ifdef __cplusplus
extern "C" {
#endif
#include "acuneus_capi.h"
#ifdef __cplusplus
}
#endif
#endif

#include "juce_core/juce_core.h"
#include "juce_gui_extra/juce_gui_extra.h"
#include "juce_opengl/juce_opengl.h"

#include <algorithm>
#include <cmath>
#include <set>

#if BESPOKE_WINDOWS
#include <windows.h>
#undef LoadString
#undef min
#undef max
#endif

namespace
{
   constexpr int kParamStartY = 166;
   constexpr int kParamX = 5;
   constexpr int kParamRowHeight = 18;
   constexpr int kParamColumnGap = 8;
   constexpr int kGroupColumnGap = 10;
   constexpr int kParamHeaderHeight = 16;
   constexpr int kSingleColumnModuleWidth = 210;
   constexpr int kGroupColumnTargetWidth = 160;
   constexpr int kDefaultRemotePort = 7841;
   constexpr int kMinRemotePort = 1024;
   constexpr int kMaxRemotePort = 65535;
   constexpr size_t kMaxPcmQueuedSamples = 44100 * 2;

   void PumpPendingMouseMessagesAfterWindowMove()
   {
#if BESPOKE_WINDOWS
      static int sMoveCount = 0;
      ++sMoveCount;
      if (sMoveCount % 4 != 0)
         return;

      MSG msg;
      constexpr int kMaxMessagesToPump = 8;
      for (int i = 0; i < kMaxMessagesToPump; ++i)
      {
         if (!PeekMessage(&msg, nullptr, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE))
            break;

         TranslateMessage(&msg);
         DispatchMessage(&msg);
      }
#endif
   }

#ifndef BESPOKE_ACUNEUS_EXECUTABLE_DIR
#define BESPOKE_ACUNEUS_EXECUTABLE_DIR ""
#endif

#if !BESPOKE_ACUNEUS_ENABLED
   enum CuneusParamType
   {
      CUNEUS_PARAM_F32 = 0,
      CUNEUS_PARAM_COLOR3 = 1,
      CUNEUS_PARAM_ACTION = 2,
      CUNEUS_PARAM_STRING = 3,
      CUNEUS_PARAM_BOOL = 4,
      CUNEUS_PARAM_SELECT = 5
   };
#endif

   std::vector<std::pair<int, std::string>> ParseSelectOptions(const char* options, int minValue, int maxValue)
   {
      std::vector<std::pair<int, std::string>> parsed;
      if (options != nullptr && options[0] != '\0')
      {
         juce::StringArray entries;
         entries.addTokens(juce::String(options), "|", "");
         for (const auto& entry : entries)
         {
            const int separator = entry.indexOfChar('=');
            const juce::String valueText = separator >= 0 ? entry.substring(0, separator) : entry;
            const juce::String labelText = separator >= 0 ? entry.substring(separator + 1) : entry;
            const int value = valueText.getIntValue();
            parsed.emplace_back(value, labelText.isEmpty() ? valueText.toStdString() : labelText.toStdString());
         }
      }

      if (parsed.empty() && maxValue >= minValue && maxValue - minValue <= 32)
      {
         for (int value = minValue; value <= maxValue; ++value)
            parsed.emplace_back(value, ofToString(value));
      }
      return parsed;
   }

   float OscArgToFloat(const juce::OSCArgument& arg, float fallback)
   {
      if (arg.isFloat32())
         return arg.getFloat32();
      if (arg.isInt32())
         return (float)arg.getInt32();
      return fallback;
   }

   bool OscArgToBool(const juce::OSCArgument& arg, bool fallback)
   {
      if (arg.isInt32())
         return arg.getInt32() != 0;
      if (arg.isFloat32())
         return arg.getFloat32() != 0.0f;
      return fallback;
   }

   std::string DisplayShaderName(const std::string& binName)
   {
      if (binName.rfind("cuneus-", 0) == 0)
         return binName;
      return "cuneus-" + binName;
   }

   int FeedbackPortForRemotePort(int remotePort)
   {
      return remotePort >= kMaxRemotePort ? remotePort - 1 : remotePort + 1;
   }

   std::set<int>& ReservedAcuneusPorts()
   {
      static std::set<int> sPorts;
      return sPorts;
   }

   bool IsReservedAcuneusControlName(const std::string& name)
   {
      static const std::set<std::string> sReservedNames{
         "shader",
         "anchor",
         "open",
         "close",
         "toggle overlay",
         "title",
         "set",
         "hide",
         "overlay",
         "title bar",
         "window x",
         "window y",
         "window w",
         "window h",
         "scale",
         "time",
         "fps",
         "resolution w",
         "resolution h",
         "exe dir",
         "embed",
         "port",
         "cols",
         "music auto",
         "music amt"
      };
      return sReservedNames.count(name) != 0;
   }

   std::string DynamicParamControlName(const std::string& id, const char* suffix = nullptr)
   {
      std::string name = id;
      if (suffix != nullptr && suffix[0] != '\0')
         name += suffix;
      if (IsReservedAcuneusControlName(name))
         name = "shader " + name;
      return name;
   }

   bool CanBindUdpPort(int port)
   {
      juce::DatagramSocket socket(false);
      return socket.bindToPort(port, "127.0.0.1");
   }

   bool CanReserveAcuneusPortPair(int remotePort)
   {
      const int feedbackPort = FeedbackPortForRemotePort(remotePort);
      const auto& reservedPorts = ReservedAcuneusPorts();
      if (reservedPorts.count(remotePort) != 0 || reservedPorts.count(feedbackPort) != 0)
         return false;
      return CanBindUdpPort(remotePort) && CanBindUdpPort(feedbackPort);
   }

}

Acuneus::Acuneus()
: IDrawableModule(330, 100)
, IAudioProcessor(gBufferSize)
{
   mExecutableDir = GetDefaultExecutableDir();
   ReserveRemotePort(kDefaultRemotePort);
}

std::atomic<int> Acuneus::sMusicAutomationUsers{ 0 };
std::atomic<float> Acuneus::sSharedMusicAutomationLevel{ 0.0f };

Acuneus::~Acuneus()
{
   SetMusicAutomationEnabled(false);
   CloseInstance();
   StopFeedbackReceiver();
   ClearParamControls();
   ReleaseRemotePort();
}

void Acuneus::Init()
{
   IDrawableModule::Init();
   OpenInstance();
}

void Acuneus::Exit()
{
   CloseInstance();
   StopFeedbackReceiver();
   IDrawableModule::Exit();
}

void Acuneus::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   DROPDOWN(mBinDropdown, "shader", &mSelectedBin, 120);
   mBinDropdown->DrawLabel(true);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mAnchorCheckbox, "anchor", &mAnchorWindow);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mOpenButton, "open");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mCloseButton, "close");
   UIBLOCK_NEWLINE();
   BUTTON(mToggleOverlayButton, "toggle overlay");
   UIBLOCK_SHIFTRIGHT();
   TEXTENTRY(mWindowTitleEntry, "title", 24, &mWindowTitle);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mSetTitleButton, "set");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mHideTitleBarButton, "hide");
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mOverlaySlider, "overlay", &mOverlayVisible, 0, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mTitleBarSlider, "title bar", &mTitleBarVisible, 0, 1);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mWindowXSlider, "window x", &mWindowX, -4000, 4000);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mWindowYSlider, "window y", &mWindowY, -4000, 4000);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mWindowWidthSlider, "window w", &mWindowWidth, 1, 4096);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mWindowHeightSlider, "window h", &mWindowHeight, 1, 4096);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mWindowScaleSlider, "scale", &mWindowScale, 0.1f, 4.0f);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mTimeSlider, "time", &mTime, 0, 600);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mFpsSlider, "fps", &mFps, 1, 240);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mResolutionWidthSlider, "resolution w", &mResolutionWidth, 1, 4096);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mResolutionHeightSlider, "resolution h", &mResolutionHeight, 1, 4096);
   UIBLOCK_NEWLINE();
   TEXTENTRY(mExecutableDirEntry, "exe dir", 48, &mExecutableDir);
   mExecutableDirEntry->DrawLabel(true);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mEmbeddedCheckbox, "embed", &mEmbedded);
   UIBLOCK_NEWLINE();
   INTSLIDER(mPortSlider, "port", &mRemotePort, 1024, 65535);
   UIBLOCK_SHIFTRIGHT();
   DROPDOWN(mParamColumnsDropdown, "cols", &mParamColumns, 48);
   mParamColumnsDropdown->AddLabel("1", 1);
   mParamColumnsDropdown->AddLabel("2", 2);
   mParamColumnsDropdown->AddLabel("3", 3);
   mParamColumnsDropdown->AddLabel("4", 4);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mMusicAutomationCheckbox, "music auto", &mMusicAutomation);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mMusicAutomationSlider, "music amt", &mMusicAutomationAmount, 0, 1);
   ENDUIBLOCK0();

   mSliderMenuLfoButton = new ClickButton(this, "lfo", HIDDEN_UICONTROL, HIDDEN_UICONTROL);
   mSliderMenuLooseButton = new ClickButton(this, "auto loose", HIDDEN_UICONTROL, HIDDEN_UICONTROL);
   mSliderMenuStrictButton = new ClickButton(this, "auto strict", HIDDEN_UICONTROL, HIDDEN_UICONTROL);
   mSliderMenuOffButton = new ClickButton(this, "auto off", HIDDEN_UICONTROL, HIDDEN_UICONTROL);
   mSliderMenuLfoButton->SetShowing(false);
   mSliderMenuLooseButton->SetShowing(false);
   mSliderMenuStrictButton->SetShowing(false);
   mSliderMenuOffButton->SetShowing(false);

   RefreshBinList();
   LoadSelectedWindowState();
   UpdateTitleBarButtonLabel();
}

void Acuneus::Poll()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
   {
      int32_t exitCode = 0;
      if (cuneus_instance_poll_child(mInstance, &exitCode) != CUNEUS_STATUS_OK)
      {
         const char* error = cuneus_last_error();
         const std::string errorText = error != nullptr && error[0] != '\0' ? error : "Acuneus exited unexpectedly";
         SetStatus(errorText);
         ShowErrorDialog("Acuneus exited", errorText);
         CloseInstance();
         return;
      }
   }

   if (mInstance != nullptr && mPendingDiscoveryRequests > 0 && gTime > mLastDiscoveryRequestTime + 0.25)
   {
      RequestDiscovery();
      --mPendingDiscoveryRequests;
      mLastDiscoveryRequestTime = gTime;
   }

   if (mInstance != nullptr && gTime > mLastTransportSendTime + 16.0)
      SendTransport();
   if (mInstance != nullptr && gTime > mLastMouseSendTime + 33.0)
      UpdateTrackedMouseParams();
   if (mInstance != nullptr && mPendingChromeApplies > 0)
   {
      --mPendingChromeApplies;
      ApplyOverlayVisible();
      ApplyTitleBarVisible();
   }
#endif

   if (mMusicAutomation)
      ApplyMusicAutomation();
   if (WantsAudioSpectrum())
      SendAudioSpectrum();

   if (mAnchorWindow)
   {
      ApplyAnchorWindow();
      if (mPendingAnchorApplies > 0)
      {
         --mPendingAnchorApplies;
         ApplyWindowPosition();
      }
   }
}

void Acuneus::Process(double time)
{
   ChannelBuffer* buffer = GetBuffer();
   const int bufferSize = buffer->BufferSize();
   const int channels = std::max(1, buffer->NumActiveChannels());
   const bool wantsMusicLevel = mMusicAutomation || AnyMusicAutomationEnabled();
   if (wantsMusicLevel)
   {
      float sumSquares = 0.0f;
      float peak = 0.0f;
      int sampleCount = 0;

      for (int ch = 0; ch < channels; ++ch)
      {
         float* data = buffer->GetChannel(ch);
         for (int i = 0; i < bufferSize; ++i)
            UpdateMusicAutomationFromSample(data[i], sumSquares, peak, sampleCount);
      }

      if (sampleCount > 0)
      {
         const float rms = std::sqrt(sumSquares / sampleCount);
         const float audioEnergy = std::min(1.0f, rms * 3.0f + peak * 0.35f);
         PublishMusicAutomationLevel(audioEnergy);
      }
   }

   if (WantsAudioSpectrum())
      UpdateAudioSpectrumFromBuffer(buffer, channels);

   IAudioReceiver* target = GetTarget();
   const bool isSynthBin = mOpenBinName == "synth" || GetSelectedBinName() == "synth";
   const bool wrotePcm = isSynthBin && WriteQueuedPcmToTarget(target, bufferSize);
   if (!wrotePcm && target != nullptr)
   {
      SyncBuffers(channels);
      ChannelBuffer* out = target->GetBuffer();
      for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
      {
         float* data = GetBuffer()->GetChannel(ch);
         Add(out->GetChannel(ch), data, GetBuffer()->BufferSize());
         GetVizBuffer()->WriteChunk(data, GetBuffer()->BufferSize(), ch);
      }
   }

   GetBuffer()->Reset();
}

void Acuneus::PushPcmFeedback(const juce::OSCMessage& msg)
{
   if (msg.size() < 1 || !msg[0].isBlob())
      return;

   const juce::MemoryBlock& blob = msg[0].getBlob();
   const size_t sampleCount = blob.getSize() / sizeof(float);
   if (sampleCount == 0)
      return;

   const float* samples = reinterpret_cast<const float*>(blob.getData());
   std::lock_guard<std::mutex> lock(mPcmQueueMutex);
   while (mPcmQueue.size() + sampleCount > kMaxPcmQueuedSamples && !mPcmQueue.empty())
      mPcmQueue.pop_front();
   mPcmQueue.insert(mPcmQueue.end(), samples, samples + sampleCount);
}

void Acuneus::PushAudioSpectrumFeedback(const juce::OSCMessage& msg)
{
   std::array<float, 69> spectrum{};
   bool hasSpectrum = false;

   if (msg.size() >= 1 && msg[0].isBlob())
   {
      const juce::MemoryBlock& blob = msg[0].getBlob();
      const size_t count = std::min(spectrum.size(), blob.getSize() / sizeof(float));
      if (count > 0)
      {
         const float* values = reinterpret_cast<const float*>(blob.getData());
         for (size_t i = 0; i < count; ++i)
            spectrum[i] = std::clamp(values[i], 0.0f, i == 64 ? 400.0f : 1.0f);
         hasSpectrum = true;
      }
   }
   else
   {
      const int count = std::min((int)spectrum.size(), msg.size());
      for (int i = 0; i < count; ++i)
      {
         if (msg[i].isFloat32())
         {
            spectrum[i] = std::clamp(msg[i].getFloat32(), 0.0f, i == 64 ? 400.0f : 1.0f);
            hasSpectrum = true;
         }
         else if (msg[i].isInt32())
         {
            spectrum[i] = std::clamp((float)msg[i].getInt32(), 0.0f, i == 64 ? 400.0f : 1.0f);
            hasSpectrum = true;
         }
      }
   }

   if (!hasSpectrum)
      return;

   {
      std::lock_guard<std::mutex> lock(mAudioSpectrumMutex);
      mAudioSpectrum = spectrum;
      mHasAudioSpectrum = true;
   }

   PublishMusicAutomationLevel(std::clamp(spectrum[68], 0.0f, 1.0f));
}

bool Acuneus::WriteQueuedPcmToTarget(IAudioReceiver* target, int bufferSize)
{
   if (target == nullptr || bufferSize <= 0)
      return false;

   gWorkChannelBuffer.SetNumActiveChannels(2);
   gWorkChannelBuffer.Clear();

   int frames = 0;
   {
      std::lock_guard<std::mutex> lock(mPcmQueueMutex);
      frames = std::min(bufferSize, (int)(mPcmQueue.size() / 2));
      for (int i = 0; i < frames; ++i)
      {
         gWorkChannelBuffer.GetChannel(0)[i] = mPcmQueue.front();
         mPcmQueue.pop_front();
         gWorkChannelBuffer.GetChannel(1)[i] = mPcmQueue.front();
         mPcmQueue.pop_front();
      }
   }

   if (frames <= 0)
      return false;

   const bool wantsMusicLevel = mMusicAutomation || AnyMusicAutomationEnabled();
   if (wantsMusicLevel)
   {
      float sumSquares = 0.0f;
      float peak = 0.0f;
      int sampleCount = 0;
      for (int ch = 0; ch < 2; ++ch)
      {
         float* data = gWorkChannelBuffer.GetChannel(ch);
         for (int i = 0; i < frames; ++i)
            UpdateMusicAutomationFromSample(data[i], sumSquares, peak, sampleCount);
      }

      const float rms = std::sqrt(sumSquares / std::max(1, sampleCount));
      const float audioEnergy = std::min(1.0f, rms * 3.0f + peak * 0.35f);
      PublishMusicAutomationLevel(audioEnergy);
   }

   if (WantsAudioSpectrum())
      UpdateAudioSpectrumFromBuffer(&gWorkChannelBuffer, 2);

   SyncBuffers(2);
   ChannelBuffer* out = target->GetBuffer();
   out->SetNumActiveChannels(2);
   Add(out->GetChannel(0), gWorkChannelBuffer.GetChannel(0), bufferSize);
   Add(out->GetChannel(1), gWorkChannelBuffer.GetChannel(1), bufferSize);
   GetVizBuffer()->WriteChunk(gWorkChannelBuffer.GetChannel(0), bufferSize, 0);
   GetVizBuffer()->WriteChunk(gWorkChannelBuffer.GetChannel(1), bufferSize, 1);
   return true;
}

void Acuneus::UpdateAudioSpectrumFromBuffer(ChannelBuffer* buffer, int channels)
{
   const int bufferSize = buffer->BufferSize();
   if (bufferSize <= 0)
      return;

   std::array<float, 69> spectrum{};
   constexpr float minFreq = 40.0f;
   constexpr float maxFreq = 14000.0f;
   const float nyquist = gSampleRate * 0.5f;
   const float usableMaxFreq = std::min(maxFreq, nyquist * 0.92f);

   for (int band = 0; band < 64; ++band)
   {
      const float t = (float)band / 63.0f;
      const float freq = minFreq * std::pow(usableMaxFreq / minFreq, t);
      const float omega = 2.0f * PI * freq / gSampleRate;
      const float coeff = 2.0f * std::cos(omega);
      float s0 = 0.0f;
      float s1 = 0.0f;
      float s2 = 0.0f;

      for (int i = 0; i < bufferSize; ++i)
      {
         float sample = 0.0f;
         for (int ch = 0; ch < channels; ++ch)
            sample += buffer->GetChannel(ch)[i];
         sample /= (float)channels;

         s0 = sample + coeff * s1 - s2;
         s2 = s1;
         s1 = s0;
      }

      const float power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
      const float magnitude = std::sqrt(std::max(0.0f, power)) / std::max(1, bufferSize);
      const float highBoost = 1.0f + t * 3.0f;
      spectrum[band] = std::clamp(magnitude * highBoost * 18.0f, 0.0f, 1.0f);
   }

   const auto averageRange = [&spectrum](int start, int end)
   {
      float total = 0.0f;
      int count = 0;
      for (int i = start; i <= end; ++i)
      {
         total += spectrum[i];
         ++count;
      }
      return count > 0 ? total / count : 0.0f;
   };

   spectrum[64] = TheTransport != nullptr ? TheTransport->GetTempo() : 120.0f;
   spectrum[65] = averageRange(0, 12);
   spectrum[66] = averageRange(13, 38);
   spectrum[67] = averageRange(39, 63);
   spectrum[68] = averageRange(0, 63);

   {
      std::lock_guard<std::mutex> lock(mAudioSpectrumMutex);
      for (size_t i = 0; i < mAudioSpectrum.size(); ++i)
         mAudioSpectrum[i] = ofLerp(mAudioSpectrum[i], spectrum[i], i < 64 ? 0.35f : 0.5f);
      mHasAudioSpectrum = true;
   }
}

void Acuneus::RefreshBinList()
{
   if (mBinDropdown == nullptr)
      return;

   mBinNames.clear();
   mBinDropdown->Clear();

#if BESPOKE_ACUNEUS_ENABLED
   const size_t binCount = cuneus_bin_count();
   for (size_t i = 0; i < binCount; ++i)
   {
      const char* name = cuneus_bin_name(i);
      if (name != nullptr)
      {
         mBinNames.push_back(name);
         mBinDropdown->AddLabel(DisplayShaderName(name).c_str(), (int)i);
      }
   }
#endif

   if (mBinNames.empty())
   {
      mBinNames.push_back("roto");
      mBinDropdown->AddLabel("cuneus-roto", 0);
   }

   mSelectedBin = std::clamp(mSelectedBin, 0, (int)mBinNames.size() - 1);
   if (mWindowTitle.empty() || mWindowTitle == "Acuneus")
      mWindowTitle = GetDefaultTitleForBin(GetSelectedBinName());
   if (mWindowTitleEntry != nullptr)
   {
      mWindowTitleEntry->SetText(mWindowTitle);
      mWindowTitleEntry->UpdateDisplayString();
   }
}

bool Acuneus::SetSelectedBinName(const std::string& binName)
{
   if (mBinNames.empty())
      RefreshBinList();

   for (int i = 0; i < (int)mBinNames.size(); ++i)
   {
      if (mBinNames[i] == binName)
      {
         mSelectedBin = i;
         mWindowTitle = GetDefaultTitleForBin(binName);
         if (mBinDropdown != nullptr)
            mBinDropdown->SetValue(i, gTime, false);
         if (mWindowTitleEntry != nullptr)
         {
            mWindowTitleEntry->SetText(mWindowTitle);
            mWindowTitleEntry->UpdateDisplayString();
         }
         LoadSelectedWindowState();
         return true;
      }
   }

   return false;
}

std::string Acuneus::GetSelectedBinName() const
{
   if (mSelectedBin >= 0 && mSelectedBin < (int)mBinNames.size())
      return mBinNames[mSelectedBin];
   return "roto";
}

std::string Acuneus::GetDefaultTitleForBin(const std::string& binName) const
{
#if BESPOKE_ACUNEUS_ENABLED
   const char* title = cuneus_bin_title(binName.c_str());
   if (title != nullptr && title[0] != '\0')
      return title;
#endif
   return DisplayShaderName(binName);
}

std::string Acuneus::GetDefaultExecutableDir() const
{
   std::string configuredDir = BESPOKE_ACUNEUS_EXECUTABLE_DIR;
   if (!configuredDir.empty())
      return configuredDir;

   return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory().getFullPathName().toStdString();
}

void Acuneus::OpenInstance()
{
#if BESPOKE_ACUNEUS_ENABLED
   SaveSelectedWindowState();
   CloseInstance();
   ReserveRemotePort(mRemotePort);
   const std::string binName = GetSelectedBinName();
   LoadSelectedWindowState();
   if (mWindowTitle.empty())
      mWindowTitle = GetDefaultTitleForBin(binName);
   if (mAnchorWindow)
   {
      float anchoredX = 0;
      float anchoredY = 0;
      if (CalculateAnchoredWindowPosition(anchoredX, anchoredY, true))
      {
         mWindowX = anchoredX;
         mWindowY = anchoredY;
         if (mWindowXSlider != nullptr)
            mWindowXSlider->SetValue(mWindowX, gTime, false);
         if (mWindowYSlider != nullptr)
            mWindowYSlider->SetValue(mWindowY, gTime, false);
      }
   }
   const int startupWindowX = (int)std::round(mWindowX);
   const int startupWindowY = (int)std::round(mWindowY);
   const uint32_t startupWindowWidth = (uint32_t)std::round(std::max(1.0f, mWindowWidth));
   const uint32_t startupWindowHeight = (uint32_t)std::round(std::max(1.0f, mWindowHeight));
   const bool startupTitleBarVisible = mTitleBarVisible >= 0.5f;
   StartFeedbackReceiver();
   std::string embeddedError;
   bool openedEmbedded = false;
   if (mEmbedded)
   {
      mInstance = cuneus_instance_open_embedded_with_feedback_at(binName.c_str(), (uint16_t)mRemotePort, (uint16_t)mFeedbackPort, startupWindowX, startupWindowY, startupWindowWidth, startupWindowHeight, startupTitleBarVisible);
      if (mInstance != nullptr)
      {
         openedEmbedded = true;
      }
      else
      {
         const char* error = cuneus_last_error();
         embeddedError = error != nullptr && error[0] != '\0' ? error : "embedded open failed";
      }
   }
   if (mInstance == nullptr)
      mInstance = cuneus_instance_open_with_feedback_at(binName.c_str(), mExecutableDir.c_str(), (uint16_t)mRemotePort, (uint16_t)mFeedbackPort, startupWindowX, startupWindowY, startupWindowWidth, startupWindowHeight, startupTitleBarVisible);
   if (mInstance == nullptr)
   {
      const char* error = cuneus_last_error();
      const std::string errorText = error != nullptr && error[0] != '\0' ? error : "failed to open Acuneus";
      SetStatus(!embeddedError.empty() ? embeddedError + "; fallback failed: " + errorText : errorText);
      ShowErrorDialog("Acuneus open failed", errorText);
      return;
   }

   mOpenBinName = binName;
   mPendingChromeApplies = 30;
   ApplyOverlayVisible();
   ApplyTitleBarVisible();
   ApplyWindowScale();
   ApplyWindowSize();
   if (mAnchorWindow)
   {
      ApplyAnchorWindow();
      mPendingAnchorApplies = 30;
   }
   else
   {
      ApplyWindowPosition();
   }
   ApplyTime();
   ApplyFps();
   ApplyResolution();
   if (!mWindowTitle.empty())
      cuneus_set_window_title(mInstance, mWindowTitle.c_str());
   RefreshParamControls();
   mPendingDiscoveryRequests = 8;
   SendTransport();
   std::string status = std::string("opened ") + (openedEmbedded ? "embedded " : "acuneus ") + DisplayShaderName(binName) + " osc " + std::to_string(mFeedbackPort);
   if (!embeddedError.empty())
      status += " (embedded fallback: " + embeddedError + ")";
   SetStatus(status);
#else
   SetStatus("BespokeSynth was built without Acuneus support");
#endif
}

void Acuneus::CloseInstance()
{
   CaptureCurrentWindowPosition();
   SaveSelectedWindowState();
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
   {
      cuneus_instance_free(mInstance);
      mInstance = nullptr;
   }
#else
   mInstance = nullptr;
#endif
   mOpenBinName.clear();
   {
      std::lock_guard<std::mutex> lock(mPcmQueueMutex);
      mPcmQueue.clear();
   }
   mLoadedMediaPath.clear();
   ClearParamControls();
   mPendingDiscoveryRequests = 0;
   mPendingAnchorApplies = 0;
   mPendingChromeApplies = 0;
}

void Acuneus::ClearParamControls()
{
   HideSliderAutomationMenu();
   for (auto& param : mParams)
   {
      for (auto* slider : param.sliders)
      {
         if (slider != nullptr)
            RemoveUIControl(slider);
      }
      if (param.textEntry != nullptr)
         RemoveUIControl(param.textEntry);
      if (param.button != nullptr)
         RemoveUIControl(param.button);
      if (param.checkbox != nullptr)
         RemoveUIControl(param.checkbox);
      if (param.dropdown != nullptr)
         RemoveUIControl(param.dropdown);
   }
   mParams.clear();
   mParamGroupHeaders.clear();
   mHeight = kParamStartY + 24;
}

void Acuneus::RefreshParamControls()
{
   ClearParamControls();
   UpdateModuleWidthForColumns();

#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance == nullptr)
      return;

   const size_t count = cuneus_param_count(mInstance);
   mParams.reserve(count);
   const int columns = std::clamp(mParamColumns, 1, 4);
   const int availableWidth = (int)std::round(mWidth) - kParamX * 2 - kGroupColumnGap * (columns - 1);
   const int groupWidth = std::max(60, availableWidth / columns);
   std::vector<int> columnY(columns, kParamStartY);
   std::string currentGroup;
   int groupColumn = 0;
   int groupX = kParamX;
   int y = kParamStartY;
   int actionColumn = 0;
   auto beginGroup = [&](const std::string& group) {
      auto columnIter = std::min_element(columnY.begin(), columnY.end());
      groupColumn = (int)std::distance(columnY.begin(), columnIter);
      groupX = kParamX + groupColumn * (groupWidth + kGroupColumnGap);
      y = *columnIter;
      currentGroup = group;
      actionColumn = 0;
      mParamGroupHeaders.push_back({ currentGroup, groupX, y + 10 });
      y += kParamHeaderHeight;
   };
   auto finishActionRow = [&]() {
      if (actionColumn > 0)
      {
         actionColumn = 0;
         y += kParamRowHeight;
      }
   };
   auto finishGroup = [&]() {
      finishActionRow();
      if (!currentGroup.empty())
         columnY[groupColumn] = y + 4;
   };
   for (size_t i = 0; i < count; ++i)
   {
      CuneusParamDesc desc{};
      if (cuneus_param_desc(mInstance, i, &desc) != CUNEUS_STATUS_OK || desc.id == nullptr)
         continue;

      const std::string group = desc.group != nullptr && desc.group[0] != '\0' ? desc.group : "Parameters";
      if (group != currentGroup)
      {
         finishGroup();
         beginGroup(group);
      }

      mParams.emplace_back();
      ParamControl& param = mParams.back();
      param.id = desc.id;
      param.label = desc.label != nullptr && desc.label[0] != '\0' ? desc.label : desc.id;
      param.group = group;
      param.type = (int)desc.param_type;
      param.values[0] = desc.default_value;
      param.values[1] = desc.default_value;
      param.values[2] = desc.default_value;
      param.defaultValues[0] = desc.default_value;
      param.defaultValues[1] = desc.default_value;
      param.defaultValues[2] = desc.default_value;
      param.minValue = desc.min_value;
      param.maxValue = desc.max_value;
      param.boolValue = desc.default_value >= 0.5f;
      param.selectValue = (int)std::round(desc.default_value);

      if (desc.param_type == CUNEUS_PARAM_ACTION)
      {
         const int actionColumns = groupWidth >= 220 ? 3 : groupWidth >= 140 ? 2 : 1;
         const int buttonWidth = std::max(42, (groupWidth - kParamColumnGap * (actionColumns - 1)) / actionColumns);
         const int x = groupX + actionColumn * (buttonWidth + kParamColumnGap);
         param.button = new ClickButton(this, DynamicParamControlName(param.id, "_action").c_str(), x, y, ButtonDisplayStyle::kText);
         param.button->SetDimensions(buttonWidth, 15);
         if (++actionColumn >= actionColumns)
            finishActionRow();
      }
      else if (desc.param_type == CUNEUS_PARAM_STRING)
      {
         finishActionRow();
         param.textEntry = new TextEntry(this, DynamicParamControlName(param.id).c_str(), groupX, y, 16, &param.stringValue);
         param.textEntry->DrawLabel(true);
         param.textEntry->SetOverrideWidth(std::max(48, groupWidth - 42));
         if (param.id == "media_path")
         {
            param.button = new ClickButton(this, DynamicParamControlName(param.id, "_load").c_str(), groupX + groupWidth - 38, y, ButtonDisplayStyle::kText);
            param.button->SetDimensions(38, 15);
         }
         y += kParamRowHeight;
      }
      else if (desc.param_type == CUNEUS_PARAM_BOOL)
      {
         finishActionRow();
         param.checkbox = new Checkbox(this, DynamicParamControlName(param.id).c_str(), groupX, y, &param.boolValue);
         y += kParamRowHeight;
      }
      else if (desc.param_type == CUNEUS_PARAM_SELECT)
      {
         finishActionRow();
         param.selectOptions = ParseSelectOptions(desc.options, (int)std::round(desc.min_value), (int)std::round(desc.max_value));
         param.dropdown = new DropdownList(this, DynamicParamControlName(param.id).c_str(), groupX, y, &param.selectValue, groupWidth);
         param.dropdown->DrawLabel(true);
         bool hasDefault = false;
         for (const auto& option : param.selectOptions)
         {
            param.dropdown->AddLabel(option.second.c_str(), option.first);
            hasDefault |= option.first == param.selectValue;
         }
         if (!hasDefault)
            param.dropdown->AddLabel(ofToString(param.selectValue).c_str(), param.selectValue);
         param.values[0] = (float)param.selectValue;
         y += kParamRowHeight;
      }
      else if (desc.param_type == CUNEUS_PARAM_COLOR3)
      {
         finishActionRow();
         const int colorSliderWidth = std::max(18, (groupWidth - kParamColumnGap * 2) / 3);
         param.sliders[0] = new FloatSlider(this, DynamicParamControlName(param.id, "_r").c_str(), groupX, y, colorSliderWidth, 15, &param.values[0], desc.min_value, desc.max_value);
         param.sliders[1] = new FloatSlider(this, DynamicParamControlName(param.id, "_g").c_str(), groupX + colorSliderWidth + kParamColumnGap, y, colorSliderWidth, 15, &param.values[1], desc.min_value, desc.max_value);
         param.sliders[2] = new FloatSlider(this, DynamicParamControlName(param.id, "_b").c_str(), groupX + (colorSliderWidth + kParamColumnGap) * 2, y, colorSliderWidth, 15, &param.values[2], desc.min_value, desc.max_value);
         y += kParamRowHeight;
      }
      else
      {
         finishActionRow();
         param.sliders[0] = new FloatSlider(this, DynamicParamControlName(param.id).c_str(), groupX, y, groupWidth, 15, &param.values[0], desc.min_value, desc.max_value);
         y += kParamRowHeight;
      }
      SetParamSliderDisplayNames(param);

      SendParam(param);
   }
   finishGroup();

   mHeight = std::max(100, *std::max_element(columnY.begin(), columnY.end()) + 20);
#endif
}

void Acuneus::SendParam(ParamControl& param)
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance == nullptr)
      return;

   if (param.type == CUNEUS_PARAM_ACTION)
      cuneus_trigger_action(mInstance, param.id.c_str(), param.values[0]);
   else if (param.type == CUNEUS_PARAM_STRING)
      cuneus_set_param_string(mInstance, param.id.c_str(), param.stringValue.c_str());
   else if (param.type == CUNEUS_PARAM_BOOL)
      cuneus_set_param_bool(mInstance, param.id.c_str(), param.boolValue);
   else if (param.type == CUNEUS_PARAM_COLOR3)
      cuneus_set_param_color3(mInstance, param.id.c_str(), param.values[0], param.values[1], param.values[2]);
   else if (param.type == CUNEUS_PARAM_SELECT)
      cuneus_set_param_f32(mInstance, param.id.c_str(), (float)param.selectValue);
   else
      cuneus_set_param_f32(mInstance, param.id.c_str(), param.values[0]);
#endif
}

void Acuneus::SendAction(ParamControl& param, float value)
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_trigger_action(mInstance, param.id.c_str(), value);
#endif
}

void Acuneus::EnableMusicAutomation(bool enabled)
{
   SetMusicAutomationEnabled(enabled);

   if (mMusicAutomationCheckbox != nullptr)
      mMusicAutomationCheckbox->SetValue(enabled ? 1.0f : 0.0f, gTime, false);
   if (mMusicAutomationSlider != nullptr)
      mMusicAutomationSlider->SetValue(mMusicAutomationAmount, gTime, false);
}

void Acuneus::SetMusicAutomationEnabled(bool enabled)
{
   mMusicAutomation = enabled;
   if (enabled)
   {
      mMusicAutomationAmount = std::max(mMusicAutomationAmount, 0.65f);
      if (!mMusicAutomationRegistered)
      {
         sMusicAutomationUsers.fetch_add(1);
         mMusicAutomationRegistered = true;
         mLastMusicAutomationSendTime = -9999;
      }
   }
   else
   {
      if (mMusicAutomationRegistered)
      {
         int currentUsers = sMusicAutomationUsers.load();
         while (currentUsers > 0 && !sMusicAutomationUsers.compare_exchange_weak(currentUsers, currentUsers - 1))
         {
         }
         mMusicAutomationRegistered = false;
      }
      mMusicAutomationLevel.store(0.0f);
      if (sMusicAutomationUsers.load() == 0)
         sSharedMusicAutomationLevel.store(0.0f);
   }
}

void Acuneus::UpdateMusicAutomationFromSample(float sample, float& sumSquares, float& peak, int& sampleCount) const
{
   sumSquares += sample * sample;
   peak = std::max(peak, std::abs(sample));
   ++sampleCount;
}

void Acuneus::PublishMusicAutomationLevel(float level)
{
   const float localSmoothed = ofLerp(mMusicAutomationLevel.load(), level, 0.18f);
   const float sharedSmoothed = ofLerp(sSharedMusicAutomationLevel.load(), level, 0.18f);
   mMusicAutomationLevel.store(localSmoothed);
   sSharedMusicAutomationLevel.store(sharedSmoothed);
}

void Acuneus::UpdateTrackedMouseParams()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance == nullptr || TheSynth == nullptr || TheSynth->GetMainComponent() == nullptr)
      return;

   const float width = std::max(1, TheSynth->GetMainComponent()->getWidth());
   const float height = std::max(1, TheSynth->GetMainComponent()->getHeight());
   const float mouseX = std::clamp(TheSynth->GetRawMouseX() / width, 0.0f, 1.0f);
   const float mouseY = std::clamp(TheSynth->GetRawMouseY() / height, 0.0f, 1.0f);

   for (auto& param : mParams)
   {
      float trackedValue = 0.0f;
      if (param.id == "mouse_x")
         trackedValue = mouseX;
      else if (param.id == "mouse_y")
         trackedValue = mouseY;
      else
         continue;

      if (std::abs(param.values[0] - trackedValue) < 0.001f)
         continue;

      param.values[0] = trackedValue;
      cuneus_set_param_f32(mInstance, param.id.c_str(), param.values[0]);
   }

   mLastMouseSendTime = gTime;
#endif
}

void Acuneus::ApplyMusicAutomation()
{
   if (!mMusicAutomation)
      return;

   if (mInstance == nullptr || gTime <= mLastMusicAutomationSendTime + 50.0)
      return;

   const float level = std::max(mMusicAutomationLevel.load(), sSharedMusicAutomationLevel.load());
   const float drive = std::clamp(level * std::clamp(mMusicAutomationAmount, 0.0f, 1.0f), 0.0f, 1.0f);
   if (drive < 0.01f)
      return;

   mLastMusicAutomationSendTime = gTime;
   const float timeSeconds = (float)gTime * 0.001f;
   const bool hasTargetedAutomation = std::any_of(mParams.begin(), mParams.end(), [](const ParamControl& param) {
      return param.musicAutomationMode > 0;
   });

   for (auto& param : mParams)
   {
      if ((param.type != CUNEUS_PARAM_F32 && param.type != CUNEUS_PARAM_COLOR3) || param.sliders[0] == nullptr)
         continue;
      if (hasTargetedAutomation && param.musicAutomationMode <= 0)
         continue;
      if (param.id.rfind("control_", 0) == 0 || param.id.rfind("media_", 0) == 0 || param.id.rfind("video_", 0) == 0)
         continue;
      if (param.id == "filter_type" || param.id == "resolution")
         continue;

      const float span = param.maxValue - param.minValue;
      if (span <= 0.0001f)
         continue;

      unsigned int hash = 2166136261u;
      for (char c : param.id)
         hash = (hash ^ (unsigned char)c) * 16777619u;

      const float phase = (hash % 6283) * 0.001f;
      const float speed = 0.35f + ((hash >> 9) % 100) * 0.008f;
      const float automationScale = param.musicAutomationMode == 1 ? 0.45f : param.musicAutomationMode == 2 ? 1.15f : 1.0f;
      const float depth = (0.10f + ((hash >> 17) % 100) * 0.003f) * automationScale;
      if (param.type == CUNEUS_PARAM_COLOR3)
      {
         bool changed = false;
         for (int colorIndex = 0; colorIndex < 3; ++colorIndex)
         {
            if (param.sliders[colorIndex] == nullptr)
               continue;

            const float colorPhase = phase + colorIndex * 2.0943951f;
            const float lfo = std::sin(timeSeconds * speed + colorPhase);
            const float pulse = std::sin(timeSeconds * (speed * 0.37f + 0.2f) + phase * 0.5f);
            const float centeredDefault = std::clamp(param.defaultValues[colorIndex], param.minValue, param.maxValue);
            const float colorDepth = (depth * 1.35f + 0.08f) * (0.65f + 0.35f * pulse);
            const float target = std::clamp(centeredDefault + lfo * span * colorDepth * drive, param.minValue, param.maxValue);

            if (std::abs(target - param.values[colorIndex]) < span * 0.0025f)
               continue;

            param.values[colorIndex] = target;
            changed = true;
         }

         if (changed)
            SendParam(param);
         continue;
      }

      const float lfo = std::sin(timeSeconds * speed + phase);
      const float centeredDefault = std::clamp(param.defaultValues[0], param.minValue, param.maxValue);
      const float target = std::clamp(centeredDefault + lfo * span * depth * drive, param.minValue, param.maxValue);

      if (std::abs(target - param.values[0]) >= span * 0.0025f)
      {
         param.values[0] = target;
         SendParam(param);
      }
   }
}

bool Acuneus::WantsAudioSpectrum() const
{
   const std::string selectedBin = GetSelectedBinName();
   return mOpenBinName == "audiovis" || selectedBin == "audiovis" ||
          mOpenBinName == "fft" || selectedBin == "fft";
}

void Acuneus::SendAudioSpectrum()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (!WantsAudioSpectrum() || mInstance == nullptr || gTime <= mLastAudioSpectrumSendTime + 33.0)
      return;

   std::array<float, 69> spectrum{};
   {
      std::lock_guard<std::mutex> lock(mAudioSpectrumMutex);
      if (!mHasAudioSpectrum)
         return;
      spectrum = mAudioSpectrum;
   }

   cuneus_set_audio_spectrum(mInstance, spectrum.data(), spectrum.size());
   mLastAudioSpectrumSendTime = gTime;
#endif
}

bool Acuneus::FloatSliderContextMenu(FloatSlider* slider)
{
   for (auto& param : mParams)
   {
      for (auto* paramSlider : param.sliders)
      {
         if (slider == paramSlider)
         {
            ShowSliderAutomationMenu(slider);
            return true;
         }
      }
   }

   return false;
}

void Acuneus::ShowSliderAutomationMenu(FloatSlider* slider)
{
   mSliderMenuTarget = slider;

   float x = 0.0f;
   float y = 0.0f;
   slider->GetPosition(x, y, true);
   float sliderWidth = 0.0f;
   float sliderHeight = 0.0f;
   slider->GetDimensions(sliderWidth, sliderHeight);
   y += sliderHeight + 2;

   const float buttonWidth = 74.0f;
   const float buttonHeight = 15.0f;
   const float gap = 3.0f;
   const float totalWidth = buttonWidth * 4.0f + gap * 3.0f;
   x = std::clamp(x, 3.0f, std::max(3.0f, mWidth - totalWidth - 3.0f));

   ClickButton* buttons[] = { mSliderMenuLfoButton, mSliderMenuLooseButton, mSliderMenuStrictButton, mSliderMenuOffButton };
   for (int i = 0; i < 4; ++i)
   {
      buttons[i]->SetPosition(x + i * (buttonWidth + gap), y);
      buttons[i]->SetDimensions(buttonWidth, buttonHeight);
      buttons[i]->SetShowing(true);
   }
}

void Acuneus::HideSliderAutomationMenu()
{
   mSliderMenuTarget = nullptr;
   if (mSliderMenuLfoButton != nullptr)
      mSliderMenuLfoButton->SetShowing(false);
   if (mSliderMenuLooseButton != nullptr)
      mSliderMenuLooseButton->SetShowing(false);
   if (mSliderMenuStrictButton != nullptr)
      mSliderMenuStrictButton->SetShowing(false);
   if (mSliderMenuOffButton != nullptr)
      mSliderMenuOffButton->SetShowing(false);
}

void Acuneus::SetParamMusicAutomation(FloatSlider* slider, int mode)
{
   for (auto& param : mParams)
   {
      for (auto* paramSlider : param.sliders)
      {
         if (slider == paramSlider)
         {
            param.musicAutomationMode = mode;
            if (mode > 0)
            {
               mMusicAutomationAmount = std::max(mMusicAutomationAmount, mode == 2 ? 0.9f : 0.45f);
               SetMusicAutomationEnabled(true);
               if (mMusicAutomationCheckbox != nullptr)
                  mMusicAutomationCheckbox->SetValue(1.0f, gTime, false);
               if (mMusicAutomationSlider != nullptr)
                  mMusicAutomationSlider->SetValue(mMusicAutomationAmount, gTime, false);
               SetStatus(param.label + (mode == 2 ? " strict music auto" : " loose music auto"));
            }
            else
            {
               SetStatus(param.label + " music auto off");
            }
            return;
         }
      }
   }
}

void Acuneus::LoadMediaFile(ParamControl* param)
{
#if BESPOKE_ACUNEUS_ENABLED
   juce::FileChooser chooser("Load media", juce::File(""), "*", true, false, TheSynth != nullptr ? TheSynth->GetFileChooserParent() : nullptr);
   if (!chooser.browseForFileToOpen())
      return;

   const std::string path = chooser.getResult().getFullPathName().replace("\\", "/").toStdString();
   if (path.empty())
      return;

   SetMediaPath(path);
#endif
}

void Acuneus::SetMediaPath(const std::string& path)
{
#if BESPOKE_ACUNEUS_ENABLED
   if (path.empty())
   {
      UnloadMedia();
      return;
   }

   std::string normalizedPath = path;
   std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
   if (normalizedPath == mLoadedMediaPath)
      return;

   ParamControl* param = nullptr;
   for (auto& candidate : mParams)
   {
      if (candidate.id == "media_path")
      {
         param = &candidate;
         break;
      }
   }

   if (param != nullptr)
   {
      param->stringValue = normalizedPath;
      if (param->textEntry != nullptr)
      {
         param->textEntry->SetText(normalizedPath);
         param->textEntry->UpdateDisplayString();
      }
      SendParam(*param);
   }
   if (mInstance == nullptr)
      return;

   if (cuneus_load_media(mInstance, normalizedPath.c_str()) != CUNEUS_STATUS_OK)
   {
      const char* error = cuneus_last_error();
      const std::string errorText = error != nullptr && error[0] != '\0' ? error : "failed to load media";
      SetStatus(errorText);
      ShowErrorDialog("Acuneus load failed", "Could not load:\n" + normalizedPath + "\n\n" + errorText);
      return;
   }

   mLoadedMediaPath = normalizedPath;
   SetStatus("loaded media " + juce::File(normalizedPath).getFileName().toStdString());
#endif
}

void Acuneus::UnloadMedia()
{
#if BESPOKE_ACUNEUS_ENABLED
   for (auto& candidate : mParams)
   {
      if (candidate.id == "media_path")
      {
         candidate.stringValue.clear();
         if (candidate.textEntry != nullptr)
         {
            candidate.textEntry->SetText("");
            candidate.textEntry->UpdateDisplayString();
         }
         break;
      }
   }

   if (mInstance != nullptr)
      cuneus_trigger_action(mInstance, "media_unload", 1.0f);

   mLoadedMediaPath.clear();
   SetStatus("unloaded media");
#endif
}

void Acuneus::ApplyOverlayVisible()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_set_overlay_visible(mInstance, mOverlayVisible >= 0.5f);
#endif
}

void Acuneus::ApplyTitleBarVisible()
{
   UpdateTitleBarButtonLabel();
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_set_window_title_bar_visible(mInstance, mTitleBarVisible >= 0.5f);
#endif
}

void Acuneus::ApplyWindowPosition()
{
   SaveSelectedWindowState();
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr && mOpenBinName == GetSelectedBinName())
   {
      cuneus_set_window_position(mInstance, (int)std::round(mWindowX), (int)std::round(mWindowY));
      PumpPendingMouseMessagesAfterWindowMove();
   }
#endif
}

void Acuneus::CaptureCurrentWindowPosition()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance == nullptr || mOpenBinName != GetSelectedBinName())
      return;

   int32_t x = 0;
   int32_t y = 0;
   if (cuneus_get_window_position(mInstance, &x, &y) != CUNEUS_STATUS_OK)
      return;

   mWindowX = (float)x;
   mWindowY = (float)y;
   if (mWindowXSlider != nullptr)
      mWindowXSlider->SetValue(mWindowX, gTime, false);
   if (mWindowYSlider != nullptr)
      mWindowYSlider->SetValue(mWindowY, gTime, false);
#endif
}

bool Acuneus::CalculateAnchoredWindowPosition(float& x, float& y, bool includeSavedOffset)
{
   ModuleContainer* container = GetOwningContainer();
   if (container == nullptr || TheSynth == nullptr || TheSynth->GetMainComponent() == nullptr)
      return false;

   float moduleX = 0;
   float moduleY = 0;
   float moduleWidth = 0;
   float moduleHeight = 0;
   GetPosition(moduleX, moduleY);
   GetDimensions(moduleWidth, moduleHeight);

   const float titleBarHeight = HasTitleBar() ? IDrawableModule::TitleBarHeight() : 0.0f;
   const float gap = 8.0f;
   float screenScale = std::max(1.0, TheSynth->GetPixelRatio());
   juce::Rectangle<int> safeArea;
   if (const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(TheSynth->GetMainComponent()->getScreenBounds()))
   {
      safeArea = display->userArea;
      screenScale = std::max(1.0, display->scale);
   }

   const float windowWidth = std::max(1.0f, mWindowWidth / (float)screenScale);
   const float windowHeight = std::max(1.0f, mWindowHeight / (float)screenScale);
   const float anchorX = moduleX + moduleWidth + gap;
   const float anchorY = moduleY - titleBarHeight + (moduleHeight + titleBarHeight - windowHeight / container->GetDrawScale()) * 0.5f;

   float logicalX = (anchorX + container->GetDrawOffset().x) * container->GetDrawScale() - UserPrefs.mouse_offset_x.Get() + TheSynth->GetMainComponent()->getScreenX();
   float logicalY = (anchorY + container->GetDrawOffset().y) * container->GetDrawScale() - UserPrefs.mouse_offset_y.Get() + TheSynth->GetMainComponent()->getScreenY();

   float physicalX = logicalX * (float)screenScale;
   float physicalY = logicalY * (float)screenScale;
   if (includeSavedOffset)
   {
      physicalX += mAnchorOffsetX;
      physicalY += mAnchorOffsetY;
   }

   if (!safeArea.isEmpty())
   {
      const float minX = (float)safeArea.getX() * (float)screenScale;
      const float minY = (float)safeArea.getY() * (float)screenScale;
      const float maxX = ((float)safeArea.getRight() - windowWidth) * (float)screenScale;
      const float maxY = ((float)safeArea.getBottom() - windowHeight) * (float)screenScale;
      physicalX = std::clamp(physicalX, minX, std::max(minX, maxX));
      physicalY = std::clamp(physicalY, minY, std::max(minY, maxY));
   }

   x = physicalX;
   y = physicalY;
   return true;
}

void Acuneus::ApplyAnchorWindow()
{
   if (!mAnchorWindow || mInstance == nullptr || mOpenBinName != GetSelectedBinName())
      return;

   float anchoredX = 0;
   float anchoredY = 0;
   if (!CalculateAnchoredWindowPosition(anchoredX, anchoredY, true))
      return;

   if (std::abs(mWindowX - anchoredX) < 0.5f && std::abs(mWindowY - anchoredY) < 0.5f)
      return;

   mWindowX = anchoredX;
   mWindowY = anchoredY;
   if (mWindowXSlider != nullptr)
      mWindowXSlider->SetValue(mWindowX, gTime, false);
   if (mWindowYSlider != nullptr)
      mWindowYSlider->SetValue(mWindowY, gTime, false);
   ApplyWindowPosition();
}

void Acuneus::ApplyWindowScale(bool updateWindowSizeFromScale)
{
   mWindowScale = std::max(0.1f, mWindowScale);
   if (updateWindowSizeFromScale)
   {
      mWindowWidth = std::max(1.0f, (float)mWindowBaseWidth * mWindowScale);
      mWindowHeight = std::max(1.0f, (float)mWindowBaseHeight * mWindowScale);
      if (mWindowWidthSlider != nullptr)
         mWindowWidthSlider->SetValue(mWindowWidth, gTime, false);
      if (mWindowHeightSlider != nullptr)
         mWindowHeightSlider->SetValue(mWindowHeight, gTime, false);
   }
   SaveSelectedWindowState();
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr && mOpenBinName == GetSelectedBinName())
      cuneus_set_window_scale(mInstance, mWindowScale);
#endif
}

void Acuneus::ApplyWindowSize()
{
   mWindowWidth = std::max(1.0f, mWindowWidth);
   mWindowHeight = std::max(1.0f, mWindowHeight);
   SaveSelectedWindowState();
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr && mOpenBinName == GetSelectedBinName())
      cuneus_set_window_size(mInstance, (uint32_t)std::round(mWindowWidth), (uint32_t)std::round(mWindowHeight));
#endif
}

void Acuneus::SaveSelectedWindowState()
{
   const std::string binName = GetSelectedBinName();
   if (binName.empty())
      return;

   WindowState& state = mWindowStates[binName];
   if (mAnchorWindow)
   {
      float baseX = 0;
      float baseY = 0;
      if (CalculateAnchoredWindowPosition(baseX, baseY, false))
      {
         mAnchorOffsetX = mWindowX - baseX;
         mAnchorOffsetY = mWindowY - baseY;
      }
   }
   state.x = mWindowX;
   state.y = mWindowY;
   state.scale = mWindowScale;
   state.baseWidth = mWindowBaseWidth;
   state.baseHeight = mWindowBaseHeight;
   state.windowWidth = mWindowWidth;
   state.windowHeight = mWindowHeight;
   state.resolutionWidth = mResolutionWidth;
   state.resolutionHeight = mResolutionHeight;
   state.anchorOffsetX = mAnchorOffsetX;
   state.anchorOffsetY = mAnchorOffsetY;
   state.hasAnchorOffset = true;
}

void Acuneus::LoadSelectedWindowState()
{
   const std::string binName = GetSelectedBinName();
   if (binName.empty())
      return;

   auto [iter, inserted] = mWindowStates.emplace(binName, DefaultWindowStateForBin(binName));
   WindowState& state = iter->second;
   if (state.baseWidth <= 0 || state.baseHeight <= 0)
   {
      WindowState defaults = DefaultWindowStateForBin(binName);
      state.baseWidth = defaults.baseWidth;
      state.baseHeight = defaults.baseHeight;
   }
   mWindowX = state.x;
   mWindowY = state.y;
   mWindowScale = state.scale;
   mWindowBaseWidth = state.baseWidth;
   mWindowBaseHeight = state.baseHeight;
   mWindowWidth = state.windowWidth > 0 ? state.windowWidth : (float)state.baseWidth * state.scale;
   mWindowHeight = state.windowHeight > 0 ? state.windowHeight : (float)state.baseHeight * state.scale;
   mResolutionWidth = state.resolutionWidth > 0 ? state.resolutionWidth : (float)state.baseWidth;
   mResolutionHeight = state.resolutionHeight > 0 ? state.resolutionHeight : (float)state.baseHeight;
   mAnchorOffsetX = state.anchorOffsetX;
   mAnchorOffsetY = state.anchorOffsetY;
   if (mAnchorWindow && !state.hasAnchorOffset)
   {
      float baseX = 0;
      float baseY = 0;
      if (CalculateAnchoredWindowPosition(baseX, baseY, false))
      {
         mAnchorOffsetX = mWindowX - baseX;
         mAnchorOffsetY = mWindowY - baseY;
         state.anchorOffsetX = mAnchorOffsetX;
         state.anchorOffsetY = mAnchorOffsetY;
         state.hasAnchorOffset = true;
      }
   }
}

Acuneus::WindowState Acuneus::DefaultWindowStateForBin(const std::string& binName) const
{
   WindowState state;
#if BESPOKE_ACUNEUS_ENABLED
   uint32_t width = 800;
   uint32_t height = 600;
   if (cuneus_bin_default_dimensions(binName.c_str(), &width, &height))
   {
      state.baseWidth = (int)width;
      state.baseHeight = (int)height;
      state.windowWidth = (float)width;
      state.windowHeight = (float)height;
      state.resolutionWidth = (float)width;
      state.resolutionHeight = (float)height;
   }
#endif
   return state;
}

void Acuneus::ApplyTime()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_set_time(mInstance, mTime);
#endif
}

void Acuneus::ApplyFps()
{
   mFps = std::max(1.0f, mFps);
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_set_fps(mInstance, mFps);
#endif
}

void Acuneus::ApplyResolution()
{
   mResolutionWidth = std::max(1.0f, mResolutionWidth);
   mResolutionHeight = std::max(1.0f, mResolutionHeight);
   SaveSelectedWindowState();
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr && mOpenBinName == GetSelectedBinName())
      cuneus_set_resolution(mInstance, (uint32_t)std::round(mResolutionWidth), (uint32_t)std::round(mResolutionHeight));
#endif
}

void Acuneus::ReserveRemotePort(int preferredPort)
{
   preferredPort = std::clamp(preferredPort, kMinRemotePort, kMaxRemotePort - 1);
   if (preferredPort == mReservedRemotePort)
   {
      mRemotePort = mReservedRemotePort;
      mFeedbackPort = mReservedFeedbackPort;
      return;
   }

   ReleaseRemotePort();

   int candidate = preferredPort;
   for (int attempt = 0; attempt < (kMaxRemotePort - kMinRemotePort) / 2; ++attempt)
   {
      if (candidate >= kMaxRemotePort)
         candidate = kMinRemotePort + ((candidate - kMinRemotePort) % 2);

      if (CanReserveAcuneusPortPair(candidate))
      {
         mReservedRemotePort = candidate;
         mReservedFeedbackPort = FeedbackPortForRemotePort(candidate);
         ReservedAcuneusPorts().insert(mReservedRemotePort);
         ReservedAcuneusPorts().insert(mReservedFeedbackPort);
         mRemotePort = mReservedRemotePort;
         mFeedbackPort = mReservedFeedbackPort;
         if (mPortSlider != nullptr)
            mPortSlider->SetValue(mRemotePort, gTime, false);
         return;
      }

      candidate += 2;
   }

   mRemotePort = preferredPort;
   mFeedbackPort = FeedbackPortForRemotePort(preferredPort);
}

void Acuneus::ReleaseRemotePort()
{
   if (mReservedRemotePort != 0)
      ReservedAcuneusPorts().erase(mReservedRemotePort);
   if (mReservedFeedbackPort != 0)
      ReservedAcuneusPorts().erase(mReservedFeedbackPort);
   mReservedRemotePort = 0;
   mReservedFeedbackPort = 0;
}

void Acuneus::UpdateModuleWidthForColumns()
{
   const int columns = std::clamp(mParamColumns, 1, 4);
   const int targetWidth = columns <= 1
      ? kSingleColumnModuleWidth
      : kParamX * 2 + columns * kGroupColumnTargetWidth + (columns - 1) * kGroupColumnGap;
   mWidth = (float)std::max(330, targetWidth);
}

void Acuneus::UpdateTitleBarButtonLabel()
{
   if (mHideTitleBarButton != nullptr)
      mHideTitleBarButton->SetLabel(mTitleBarVisible >= 0.5f ? "hide" : "show");
}

void Acuneus::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
   if (mApplyingFeedback)
      return;

   if (slider == mOverlaySlider)
   {
      ApplyOverlayVisible();
      return;
   }
   if (slider == mTitleBarSlider)
   {
      ApplyTitleBarVisible();
      return;
   }
   if (slider == mWindowXSlider || slider == mWindowYSlider)
   {
      ApplyWindowPosition();
      return;
   }
   if (slider == mWindowWidthSlider || slider == mWindowHeightSlider)
   {
      ApplyWindowSize();
      return;
   }
   if (slider == mWindowScaleSlider)
   {
      ApplyWindowScale(true);
      return;
   }
   if (slider == mTimeSlider)
   {
      ApplyTime();
      return;
   }
   if (slider == mFpsSlider)
   {
      ApplyFps();
      return;
   }
   if (slider == mResolutionWidthSlider || slider == mResolutionHeightSlider)
   {
      ApplyResolution();
      return;
   }

   for (auto& param : mParams)
   {
      for (auto* paramSlider : param.sliders)
      {
         if (slider == paramSlider)
         {
            SendParam(param);
            return;
         }
      }
   }
}

void Acuneus::IntSliderUpdated(IntSlider* slider, int oldVal, double time)
{
   if (slider == mPortSlider)
   {
      ReserveRemotePort(mRemotePort);
      if (mInstance != nullptr)
         OpenInstance();
      return;
   }
}

void Acuneus::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mParamColumnsDropdown)
   {
      mParamColumns = std::clamp(mParamColumns, 1, 4);
      UpdateModuleWidthForColumns();
      RefreshParamControls();
      return;
   }

   if (list == mBinDropdown && oldVal != mSelectedBin)
   {
      if (oldVal >= 0 && oldVal < (int)mBinNames.size())
      {
         const int selectedBin = mSelectedBin;
         mSelectedBin = oldVal;
         SaveSelectedWindowState();
         mSelectedBin = selectedBin;
      }
      LoadSelectedWindowState();
      mWindowTitle = GetDefaultTitleForBin(GetSelectedBinName());
      if (mWindowTitleEntry != nullptr)
      {
         mWindowTitleEntry->SetText(mWindowTitle);
         mWindowTitleEntry->UpdateDisplayString();
      }
      OpenInstance();
      return;
   }

   if (mApplyingFeedback)
      return;

   for (auto& param : mParams)
   {
      if (list == param.dropdown)
      {
         param.values[0] = (float)param.selectValue;
         SendParam(param);
         return;
      }
   }
}

void Acuneus::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (mApplyingFeedback)
      return;

   if (checkbox == mAnchorCheckbox)
      ApplyAnchorWindow();
   if (checkbox == mMusicAutomationCheckbox)
   {
      SetMusicAutomationEnabled(mMusicAutomation);
      if (mMusicAutomationSlider != nullptr)
         mMusicAutomationSlider->SetValue(mMusicAutomationAmount, gTime, false);
   }
   if (checkbox == mEmbeddedCheckbox)
   {
      if (mInstance != nullptr)
         OpenInstance();
      return;
   }
   for (auto& param : mParams)
   {
      if (checkbox == param.checkbox)
      {
         param.values[0] = param.boolValue ? 1.0f : 0.0f;
         SendParam(param);
         return;
      }
   }
}

void Acuneus::ButtonClicked(ClickButton* button, double time)
{
   if (button == mSliderMenuLfoButton)
   {
      if (mSliderMenuTarget != nullptr)
         mSliderMenuTarget->DisplayLFOControl();
      HideSliderAutomationMenu();
      return;
   }
   if (button == mSliderMenuLooseButton)
   {
      SetParamMusicAutomation(mSliderMenuTarget, 1);
      HideSliderAutomationMenu();
      return;
   }
   if (button == mSliderMenuStrictButton)
   {
      SetParamMusicAutomation(mSliderMenuTarget, 2);
      HideSliderAutomationMenu();
      return;
   }
   if (button == mSliderMenuOffButton)
   {
      SetParamMusicAutomation(mSliderMenuTarget, 0);
      HideSliderAutomationMenu();
      return;
   }

   if (button == mOpenButton)
      OpenInstance();
   if (button == mCloseButton)
   {
      CloseInstance();
      SetStatus("closed");
   }
   if (button == mToggleOverlayButton)
   {
      mOverlayVisible = mOverlayVisible >= 0.5f ? 0.0f : 1.0f;
      ApplyOverlayVisible();
   }
   if (button == mSetTitleButton)
   {
#if BESPOKE_ACUNEUS_ENABLED
      if (mInstance != nullptr)
         cuneus_set_window_title(mInstance, mWindowTitle.c_str());
#endif
   }
   if (button == mHideTitleBarButton)
   {
      mTitleBarVisible = mTitleBarVisible >= 0.5f ? 0.0f : 1.0f;
      ApplyTitleBarVisible();
   }
   for (auto& param : mParams)
   {
      if (button == param.button)
      {
         if (param.type == CUNEUS_PARAM_STRING)
         {
            LoadMediaFile(&param);
            return;
         }
         param.values[0] = 1.0f;
         if (param.sliders[0] != nullptr)
            param.sliders[0]->SetValue(1.0f, time, true);
         SendAction(param, 1.0f);
         return;
      }
   }
}

void Acuneus::TextEntryComplete(TextEntry* entry)
{
   if (entry == mWindowTitleEntry)
   {
#if BESPOKE_ACUNEUS_ENABLED
      if (mInstance != nullptr)
         cuneus_set_window_title(mInstance, mWindowTitle.c_str());
#endif
   }
   for (auto& param : mParams)
   {
      if (entry == param.textEntry)
      {
         SendParam(param);
         return;
      }
   }
}

bool Acuneus::StartFeedbackReceiver()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mFeedbackReceiverConnected)
      StopFeedbackReceiver();

   const bool connected = juce::OSCReceiver::connect(mFeedbackPort);
   if (!connected)
   {
      SetStatus("osc feedback port unavailable: " + std::to_string(mFeedbackPort));
      return false;
   }

   juce::OSCReceiver::addListener(this);
   mFeedbackReceiverConnected = true;
   return true;
#else
   return false;
#endif
}

void Acuneus::StopFeedbackReceiver()
{
   if (mFeedbackReceiverConnected)
   {
      juce::OSCReceiver::removeListener(this);
      juce::OSCReceiver::disconnect();
      mFeedbackReceiverConnected = false;
   }
}

void Acuneus::RequestDiscovery()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance == nullptr)
      return;
   cuneus_subscribe(mInstance, true);
   cuneus_discover(mInstance);
#endif
}

void Acuneus::SendTransport()
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance == nullptr || TheTransport == nullptr)
      return;

   const float bpm = TheTransport->GetTempo();
   const float beat = (float)(TheTransport->GetMeasureTime(gTime) * TheTransport->GetTimeSigTop() * 4.0 / TheTransport->GetTimeSigBottom());
   const float measure = (float)TheTransport->GetMeasure(gTime);
   cuneus_set_transport(mInstance, bpm, beat, measure);
   mLastTransportSendTime = gTime;
#endif
}

void Acuneus::ApplyFeedbackValue(const std::string& id, const juce::OSCMessage& msg)
{
   if (msg.size() == 0)
      return;

   for (auto& param : mParams)
   {
      if (param.id != id)
         continue;

      mApplyingFeedback = true;
      if (param.type == CUNEUS_PARAM_COLOR3 && msg.size() >= 3)
      {
         for (int i = 0; i < 3; ++i)
            param.values[i] = OscArgToFloat(msg[i], param.values[i]);
      }
      else if (param.type == CUNEUS_PARAM_STRING && msg[0].isString())
      {
         param.stringValue = msg[0].getString().toStdString();
         if (param.textEntry != nullptr)
         {
            param.textEntry->SetText(param.stringValue);
            param.textEntry->UpdateDisplayString();
         }
      }
      else if (param.type == CUNEUS_PARAM_BOOL)
      {
         param.boolValue = OscArgToBool(msg[0], param.boolValue);
         param.values[0] = param.boolValue ? 1.0f : 0.0f;
         if (param.checkbox != nullptr)
            param.checkbox->SetValue(param.values[0], gTime, true);
      }
      else if (param.type == CUNEUS_PARAM_SELECT)
      {
         param.selectValue = (int)std::round(OscArgToFloat(msg[0], (float)param.selectValue));
         param.values[0] = (float)param.selectValue;
         if (param.dropdown != nullptr)
            param.dropdown->SetValue(param.selectValue, gTime, true);
      }
      else
      {
         param.values[0] = OscArgToFloat(msg[0], param.values[0]);
      }
      mApplyingFeedback = false;
      return;
   }
}

void Acuneus::ApplyParamDescFeedback(const juce::OSCMessage& msg)
{
   if (msg.size() < 3 || !msg[1].isString() || !msg[2].isString())
      return;

   const std::string id = msg[1].getString().toStdString();
   const std::string label = msg[2].getString().toStdString();
   const std::string group = msg.size() >= 4 && msg[3].isString() ? msg[3].getString().toStdString() : "";
   if (id.empty() || label.empty())
      return;

   for (auto& param : mParams)
   {
      if (param.id != id)
         continue;

      param.label = label;
      if (!group.empty())
         param.group = group;
      SetParamSliderDisplayNames(param);
      return;
   }
}

bool Acuneus::ApplyTransportFeedback(const std::string& address, const juce::OSCMessage& msg)
{
   if (TheTransport == nullptr || TheSynth == nullptr)
      return false;

   if (address == "/acuneus/cuneus/transport" && msg.size() >= 3)
   {
      const float bpm = OscArgToFloat(msg[0], TheTransport->GetTempo());
      const float beat = OscArgToFloat(msg[1], 0.0f);
      const float measure = OscArgToFloat(msg[2], (float)TheTransport->GetMeasure(gTime));
      const float beatsPerMeasure = TheTransport->GetTimeSigTop() * 4.0f / TheTransport->GetTimeSigBottom();
      if (bpm > 0.0f && beatsPerMeasure > 0.0f)
      {
         TheTransport->SetTempo(bpm);
         TheTransport->SetMeasureTime(measure + beat / beatsPerMeasure);
      }
      return true;
   }

   if (address == "/acuneus/cuneus/transport/tempo" && msg.size() >= 1)
   {
      const float bpm = OscArgToFloat(msg[0], TheTransport->GetTempo());
      if (bpm > 0.0f)
         TheTransport->SetTempo(bpm);
      return true;
   }

   if (address == "/acuneus/cuneus/transport/play" && msg.size() >= 1)
   {
      TheSynth->SetAudioPaused(!OscArgToBool(msg[0], !TheSynth->IsAudioPaused()));
      return true;
   }

   if (address == "/acuneus/cuneus/transport/reset")
   {
      TheTransport->Reset();
      return true;
   }

   if (address == "/acuneus/cuneus/transport/shift_beats" && msg.size() >= 1)
   {
      const float beats = OscArgToFloat(msg[0], 0.0f);
      if (TheTransport->GetTimeSigTop() > 0)
         TheTransport->SetMeasureTime(TheTransport->GetMeasureTime(gTime) + beats / TheTransport->GetTimeSigTop());
      return true;
   }

   return false;
}

void Acuneus::SetParamSliderDisplayNames(ParamControl& param)
{
   const std::string label = param.label.empty() ? param.id : param.label;
   if (param.type == CUNEUS_PARAM_ACTION)
   {
      if (param.sliders[0] != nullptr)
         param.sliders[0]->SetOverrideDisplayName(label);
      if (param.button != nullptr)
         param.button->SetLabel(label.c_str());
   }
   else if (param.type == CUNEUS_PARAM_COLOR3)
   {
      if (param.sliders[0] != nullptr)
         param.sliders[0]->SetOverrideDisplayName(label + " R");
      if (param.sliders[1] != nullptr)
         param.sliders[1]->SetOverrideDisplayName(label + " G");
      if (param.sliders[2] != nullptr)
         param.sliders[2]->SetOverrideDisplayName(label + " B");
   }
   else if (param.type == CUNEUS_PARAM_STRING)
   {
      if (param.textEntry != nullptr)
         param.textEntry->SetOverrideDisplayName(label);
   }
   else if (param.type == CUNEUS_PARAM_BOOL)
   {
      if (param.checkbox != nullptr)
         param.checkbox->SetLabel(label.c_str());
   }
   else if (param.type == CUNEUS_PARAM_SELECT)
   {
      if (param.dropdown != nullptr)
         param.dropdown->SetOverrideDisplayName(label);
   }
   else if (param.sliders[0] != nullptr)
   {
      param.sliders[0]->SetOverrideDisplayName(label);
   }
}

void Acuneus::oscMessageReceived(const juce::OSCMessage& msg)
{
   const std::string address = msg.getAddressPattern().toString().toStdString();
   const std::string lowerAddress = juce::String(address).toLowerCase().toStdString();

   if (lowerAddress == "/acuneus/cuneus/audio_pcm")
   {
      PushPcmFeedback(msg);
      return;
   }

   if (lowerAddress == "/acuneus/cuneus/audio_spectrum")
   {
      PushAudioSpectrumFeedback(msg);
      return;
   }

   if (lowerAddress == "/acuneus/cuneus/status" && msg.size() >= 1 && msg[0].isString())
   {
      SetStatus("Acuneus " + msg[0].getString().toStdString());
      return;
   }

   const std::string paramPrefix = "/acuneus/cuneus/param/";
   if (lowerAddress.rfind(paramPrefix, 0) == 0)
   {
      ApplyFeedbackValue(lowerAddress.substr(paramPrefix.length()), msg);
      return;
   }

   if (lowerAddress == "/acuneus/cuneus/bin" && msg.size() >= 1 && msg[0].isString())
   {
      SetStatus("connected " + msg[0].getString().toStdString());
      return;
   }

   if (lowerAddress == "/acuneus/cuneus/param_desc")
   {
      ApplyParamDescFeedback(msg);
      return;
   }

   if (ApplyTransportFeedback(lowerAddress, msg))
      return;
}

void Acuneus::PlayNote(NoteMessage note)
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_note(mInstance, note.pitch, note.velocity / 127.0f);
#endif
}

void Acuneus::OnPulse(double time, float velocity, int flags)
{
#if BESPOKE_ACUNEUS_ENABLED
   if (mInstance != nullptr)
      cuneus_pulse(mInstance, velocity);
#endif
}

void Acuneus::SetStatus(std::string status)
{
   mStatus = status;
}

void Acuneus::ShowErrorDialog(const std::string& title, const std::string& error)
{
   const juce::String errorText(error);
   auto options = juce::MessageBoxOptions()
                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                     .withTitle(juce::String(title))
                     .withMessage(errorText)
                     .withButton("Copy Error")
                     .withButton("Close")
                     .withAssociatedComponent(TheSynth != nullptr ? TheSynth->GetFileChooserParent() : nullptr);

   juce::NativeMessageBox::showAsync(options, [errorText](int result)
   {
      if (result == 1)
      {
         if (TheSynth != nullptr)
            TheSynth->CopyTextToClipboard(errorText);
         else
            juce::SystemClipboard::copyTextToClipboard(errorText);
      }
   });
}

void Acuneus::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mBinDropdown->Draw();
   mAnchorCheckbox->Draw();
   mOpenButton->Draw();
   mCloseButton->Draw();
   mToggleOverlayButton->Draw();
   mWindowTitleEntry->Draw();
   mSetTitleButton->Draw();
   mHideTitleBarButton->Draw();
   mOverlaySlider->Draw();
   mTitleBarSlider->Draw();
   mWindowXSlider->Draw();
   mWindowYSlider->Draw();
   mWindowWidthSlider->Draw();
   mWindowHeightSlider->Draw();
   mWindowScaleSlider->Draw();
   mTimeSlider->Draw();
   mFpsSlider->Draw();
   mResolutionWidthSlider->Draw();
   mResolutionHeightSlider->Draw();
   mExecutableDirEntry->Draw();
   mEmbeddedCheckbox->Draw();
   mPortSlider->Draw();
   mParamColumnsDropdown->Draw();
   mMusicAutomationCheckbox->Draw();
   mMusicAutomationSlider->Draw();

   ofPushStyle();
   ofSetColor(210, 210, 210, 255);
   for (const auto& header : mParamGroupHeaders)
      DrawTextNormal(header.label, header.x, header.y, 9);
   ofPopStyle();

   for (auto& param : mParams)
   {
      const bool useParamColor = param.type == CUNEUS_PARAM_COLOR3;
      if (useParamColor)
      {
         IUIControl::sUseOverrideColor = true;
         IUIControl::sCurrentOverrideColor = ofColor(
            (int)std::round(std::clamp(param.values[0], 0.0f, 1.0f) * 255.0f),
            (int)std::round(std::clamp(param.values[1], 0.0f, 1.0f) * 255.0f),
            (int)std::round(std::clamp(param.values[2], 0.0f, 1.0f) * 255.0f),
            gModuleDrawAlpha);
      }
      for (auto* slider : param.sliders)
      {
         if (slider != nullptr)
            slider->Draw();
      }
      if (useParamColor)
         IUIControl::sUseOverrideColor = false;
      if (param.textEntry != nullptr)
         param.textEntry->Draw();
      if (param.button != nullptr)
         param.button->Draw();
      if (param.checkbox != nullptr)
         param.checkbox->Draw();
   }

   if (mSliderMenuLfoButton != nullptr && mSliderMenuLfoButton->IsShowing())
   {
      ofPushStyle();
      const auto rect = mSliderMenuLfoButton->GetRect(true);
      ofSetColor(12, 12, 12, 235);
      ofFill();
      ofRect(rect.x - 4, rect.y - 4, 320, 23, 4);
      ofPopStyle();

      mSliderMenuLfoButton->Draw();
      mSliderMenuLooseButton->Draw();
      mSliderMenuStrictButton->Draw();
      mSliderMenuOffButton->Draw();
   }

   ofPushStyle();
   ofSetColor(18, 18, 18, 210);
   ofRect(5, mHeight - 20, mWidth - 10, 15);
   ofSetColor(235, 235, 235, 255);
   DrawTextNormal(mStatus.empty() ? "ready" : mStatus, 8, mHeight - 8, 8);
   ofPopStyle();
}

void Acuneus::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("executable_dir", moduleInfo, GetDefaultExecutableDir());
   mModuleSaveData.LoadString("window_title", moduleInfo, "Acuneus");
   mModuleSaveData.LoadInt("remote_port", moduleInfo, 7841, 1024, 65535);
   mModuleSaveData.LoadInt("selected_bin", moduleInfo, 0, 0, 1024);
   mModuleSaveData.LoadInt("param_columns", moduleInfo, 2, 1, 4);
   mModuleSaveData.LoadBool("embedded", moduleInfo, false);
   mModuleSaveData.LoadBool("anchor_window", moduleInfo, true);
   mModuleSaveData.LoadFloat("overlay_visible", moduleInfo, 1.0f, 0.0f, 1.0f);
   mModuleSaveData.LoadFloat("title_bar_visible", moduleInfo, 1.0f, 0.0f, 1.0f);
   mModuleSaveData.LoadFloat("time", moduleInfo, 0.0f, 0.0f, 600.0f);
   mModuleSaveData.LoadFloat("fps", moduleInfo, 60.0f, 1.0f, 240.0f);
   mModuleSaveData.LoadBool("music_automation", moduleInfo, false);
   mModuleSaveData.LoadFloat("music_automation_amount", moduleInfo, 0.65f, 0.0f, 1.0f);
   mWindowStates.clear();
   const Json::Value& windowStates = moduleInfo["window_states"];
   if (windowStates.isObject())
   {
      std::vector<std::string> binNames = windowStates.getMemberNames();
      for (const auto& binName : binNames)
      {
         const Json::Value& value = windowStates[binName];
         WindowState state = DefaultWindowStateForBin(binName);
         if (!value["x"].isNull())
            state.x = value["x"].asFloat();
         if (!value["y"].isNull())
            state.y = value["y"].asFloat();
         if (!value["scale"].isNull())
            state.scale = value["scale"].asFloat();
         if (!value["base_width"].isNull())
            state.baseWidth = value["base_width"].asInt();
         if (!value["base_height"].isNull())
            state.baseHeight = value["base_height"].asInt();
         if (!value["window_width"].isNull())
            state.windowWidth = value["window_width"].asFloat();
         if (!value["window_height"].isNull())
            state.windowHeight = value["window_height"].asFloat();
         if (!value["resolution_width"].isNull())
            state.resolutionWidth = value["resolution_width"].asFloat();
         if (!value["resolution_height"].isNull())
            state.resolutionHeight = value["resolution_height"].asFloat();
         if (!value["anchor_offset_x"].isNull())
         {
            state.anchorOffsetX = value["anchor_offset_x"].asFloat();
            state.hasAnchorOffset = true;
         }
         if (!value["anchor_offset_y"].isNull())
         {
            state.anchorOffsetY = value["anchor_offset_y"].asFloat();
            state.hasAnchorOffset = true;
         }
         mWindowStates[binName] = state;
      }
   }
   SetUpFromSaveData();
}

void Acuneus::SaveLayout(ofxJSONElement& moduleInfo)
{
   SaveSelectedWindowState();
   moduleInfo["executable_dir"] = mExecutableDir;
   moduleInfo["window_title"] = mWindowTitle;
   moduleInfo["remote_port"] = mRemotePort;
   moduleInfo["selected_bin"] = mSelectedBin;
   moduleInfo["param_columns"] = mParamColumns;
   moduleInfo["embedded"] = mEmbedded;
   moduleInfo["anchor_window"] = mAnchorWindow;
   moduleInfo["overlay_visible"] = mOverlayVisible;
   moduleInfo["title_bar_visible"] = mTitleBarVisible;
   moduleInfo["time"] = mTime;
   moduleInfo["fps"] = mFps;
   moduleInfo["music_automation"] = mMusicAutomation;
   moduleInfo["music_automation_amount"] = mMusicAutomationAmount;
   Json::Value windowStates(Json::objectValue);
   for (const auto& [binName, state] : mWindowStates)
   {
      windowStates[binName]["x"] = state.x;
      windowStates[binName]["y"] = state.y;
      windowStates[binName]["scale"] = state.scale;
      windowStates[binName]["base_width"] = state.baseWidth;
      windowStates[binName]["base_height"] = state.baseHeight;
      windowStates[binName]["window_width"] = state.windowWidth;
      windowStates[binName]["window_height"] = state.windowHeight;
      windowStates[binName]["resolution_width"] = state.resolutionWidth;
      windowStates[binName]["resolution_height"] = state.resolutionHeight;
      windowStates[binName]["anchor_offset_x"] = state.anchorOffsetX;
      windowStates[binName]["anchor_offset_y"] = state.anchorOffsetY;
   }
   moduleInfo["window_states"] = windowStates;
}

void Acuneus::SetUpFromSaveData()
{
   mExecutableDir = mModuleSaveData.GetString("executable_dir");
   mWindowTitle = mModuleSaveData.GetString("window_title");
   ReserveRemotePort(mModuleSaveData.GetInt("remote_port"));
   mSelectedBin = mModuleSaveData.GetInt("selected_bin");
   mParamColumns = std::clamp(mModuleSaveData.GetInt("param_columns"), 1, 4);
   mEmbedded = mModuleSaveData.GetBool("embedded");
   mAnchorWindow = mModuleSaveData.GetBool("anchor_window");
   mOverlayVisible = mModuleSaveData.GetFloat("overlay_visible");
   mTitleBarVisible = mModuleSaveData.GetFloat("title_bar_visible");
   mTime = mModuleSaveData.GetFloat("time");
   mFps = mModuleSaveData.GetFloat("fps");
   mMusicAutomationAmount = mModuleSaveData.GetFloat("music_automation_amount");
   SetMusicAutomationEnabled(mModuleSaveData.GetBool("music_automation"));

   if (mExecutableDirEntry != nullptr)
   {
      mExecutableDirEntry->SetText(mExecutableDir);
      mExecutableDirEntry->UpdateDisplayString();
   }
   if (mWindowTitleEntry != nullptr)
   {
      mWindowTitleEntry->SetText(mWindowTitle);
      mWindowTitleEntry->UpdateDisplayString();
   }
   if (mMusicAutomationCheckbox != nullptr)
      mMusicAutomationCheckbox->SetValue(mMusicAutomation ? 1.0f : 0.0f, gTime, false);
   if (mMusicAutomationSlider != nullptr)
      mMusicAutomationSlider->SetValue(mMusicAutomationAmount, gTime, false);
   UpdateTitleBarButtonLabel();
   RefreshBinList();
   LoadSelectedWindowState();
}

void Acuneus::SaveState(FileStreamOut& out)
{
   SaveSelectedWindowState();
   IDrawableModule::SaveState(out);
   out << mExecutableDir;
   out << mWindowTitle;
   out << mRemotePort;
   out << mSelectedBin;
   out << mParamColumns;
   out << mEmbedded;
   out << mAnchorWindow;
   out << mOverlayVisible;
   out << mTitleBarVisible;
   out << mTime;
   out << mFps;
   out << mMusicAutomation;
   out << mMusicAutomationAmount;
   out << (int)mWindowStates.size();
   for (const auto& [binName, state] : mWindowStates)
   {
      out << binName;
      out << state.x;
      out << state.y;
      out << state.scale;
      out << state.baseWidth;
      out << state.baseHeight;
      out << state.windowWidth;
      out << state.windowHeight;
      out << state.resolutionWidth;
      out << state.resolutionHeight;
      out << state.anchorOffsetX;
      out << state.anchorOffsetY;
   }
}

void Acuneus::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev >= 1)
   {
      in >> mExecutableDir;
      if (rev >= 4)
         in >> mWindowTitle;
      in >> mRemotePort;
      in >> mSelectedBin;
      if (rev >= 11)
         in >> mParamColumns;
      else
         mParamColumns = 2;
      if (rev >= 2)
         in >> mEmbedded;
      if (rev >= 3)
         in >> mAnchorWindow;
      if (rev >= 5)
      {
         in >> mOverlayVisible;
         in >> mTitleBarVisible;
      }
      if (rev >= 7)
      {
         in >> mTime;
         in >> mFps;
      }
      if (rev >= 12)
      {
         in >> mMusicAutomation;
         in >> mMusicAutomationAmount;
      }
      else
      {
         mMusicAutomation = false;
         mMusicAutomationAmount = 0.65f;
      }
      if (rev >= 6)
      {
         int count = 0;
         in >> count;
         mWindowStates.clear();
         for (int i = 0; i < count; ++i)
         {
            std::string binName;
            WindowState state;
            in >> binName;
            in >> state.x;
            in >> state.y;
            in >> state.scale;
            in >> state.baseWidth;
            in >> state.baseHeight;
            if (rev >= 10)
            {
               in >> state.windowWidth;
               in >> state.windowHeight;
            }
            else
            {
               state.windowWidth = (float)state.baseWidth * state.scale;
               state.windowHeight = (float)state.baseHeight * state.scale;
            }
            if (rev >= 7)
            {
               in >> state.resolutionWidth;
               in >> state.resolutionHeight;
            }
            else
            {
               state.resolutionWidth = (float)state.baseWidth;
               state.resolutionHeight = (float)state.baseHeight;
            }
            if (rev >= 9)
            {
               in >> state.anchorOffsetX;
               in >> state.anchorOffsetY;
               state.hasAnchorOffset = true;
            }
            mWindowStates[binName] = state;
         }
      }
   }
   SetUpFromSaveData();
}

std::vector<IUIControl*> Acuneus::ControlsToIgnoreInSaveState() const
{
   std::vector<IUIControl*> ignore;
   ignore.push_back(mOpenButton);
   ignore.push_back(mCloseButton);
   ignore.push_back(mToggleOverlayButton);
   ignore.push_back(mHideTitleBarButton);
   ignore.push_back(mSetTitleButton);
   ignore.push_back(mSliderMenuLfoButton);
   ignore.push_back(mSliderMenuLooseButton);
   ignore.push_back(mSliderMenuStrictButton);
   ignore.push_back(mSliderMenuOffButton);
   for (const auto& param : mParams)
   {
      if (param.button != nullptr)
         ignore.push_back(param.button);
   }
   return ignore;
}
