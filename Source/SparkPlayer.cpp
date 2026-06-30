/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "SparkPlayer.h"
#include "IAudioReceiver.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "SynthGlobals.h"
#include "UIControlMacros.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#if SPARKPLAYER_POSIX_SHM
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
   constexpr uint32_t kMagic = 'S' | ('P' << 8) | ('R' << 16) | ('K' << 24);
   constexpr uint32_t kVersion = 1;
   constexpr int kOutputChannels = 2;
   constexpr double kMaxBufferedSeconds = 0.25;
   constexpr float kAutoVisualizerThreshold = 0.025f;
   constexpr double kAutoVisualizerMinDelayMs = 2500.0;
   constexpr double kAutoVisualizerMaxDelayMs = 7000.0;
   constexpr float kAutoVisualizerLevelRange = 0.18f;
   constexpr float kAutoVisualizerGateScale = 0.72f;

#if BESPOKE_WINDOWS
   std::string ToWindowsName(const std::string& text)
   {
      if (text == "Local\\SparkPlayerAudio" || text == "Local\\sparkplayer_audio")
         return "Local\\SparkPlayerAudio";
      if (text.rfind("Local\\", 0) == 0 || text.rfind("Global\\", 0) == 0)
         return text;

      std::string raw = text;
      if (raw.empty())
         raw = "sparkplayer_audio";
      if (!raw.empty() && raw[0] == '/')
         raw = raw.substr(1);
      else
      {
         const size_t slash = raw.find_last_of("\\/:");
         if (slash != std::string::npos)
            raw = raw.substr(slash + 1);
      }

      if (raw == "SparkPlayerAudio" || raw == "sparkplayer_audio")
         return "Local\\SparkPlayerAudio";

      std::string normalized = "Local\\";
      for (char c : raw)
      {
         const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
         normalized.push_back(valid ? c : '_');
      }
      if (normalized == "Local\\")
         normalized += "SparkPlayerAudio";
      return normalized;
   }

   std::wstring ToWide(const std::string& text)
   {
      if (text.empty())
         return std::wstring();
      const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
      if (needed <= 0)
         return std::wstring(text.begin(), text.end());
      std::wstring wide((size_t)needed, L'\0');
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), needed);
      if (!wide.empty() && wide.back() == L'\0')
         wide.pop_back();
      return wide;
   }
#endif

#if SPARKPLAYER_POSIX_SHM
   std::string ToPosixName(const std::string& text)
   {
      std::string raw = text;
      if (raw.empty())
         raw = "sparkplayer_audio";
      if (!raw.empty() && raw[0] == '/')
         raw = raw.substr(1);
      else
      {
         const size_t slash = raw.find_last_of("\\/:");
         if (slash != std::string::npos)
            raw = raw.substr(slash + 1);
      }

      std::string normalized = "/";
      for (char c : raw)
      {
         const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
         normalized.push_back(valid ? c : '_');
      }
      if (normalized.size() == 1)
         normalized += "sparkplayer_audio";
      return normalized;
   }
#endif
}

SparkPlayer::SparkPlayer()
{
}

void SparkPlayer::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK(3, 58);
   BUTTON(mVisualizerPrevButton, "vis-");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mVisualizerNextButton, "vis+");
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mSyncTransportCheckbox, "sync transport", &mSyncTransport);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mAutoVisualizerCheckbox, "auto viz", &mAutoVisualizer);
   ENDUIBLOCK0();
}
void SparkPlayer::Poll()
{
   IDrawableModule::Poll();
   PublishTransportControl();
   PublishAutoVisualizerControl();
}

SparkPlayer::~SparkPlayer()
{
   CloseMapping();
}

void SparkPlayer::Process(double time)
{
   PROFILER(SparkPlayer);

   IAudioReceiver* target = GetTarget();
   if (!mEnabled || target == nullptr)
      return;

   const int bufferSize = target->GetBuffer()->BufferSize();
   if (bufferSize <= 0)
      return;

   if (!EnsureOpen())
      return;

#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM

   const uint64_t generation = mHeader->generation;
   const uint64_t writeFrame = mHeader->writeFrame;
   mLastWriteFrame = writeFrame;
   if (generation != mGeneration)
   {
      mGeneration = generation;
      ResetReaderToWriter();
   }

   const double sourceRate = std::max(1u, mHeader->sampleRate);
   const double step = sourceRate / std::max(1.0, (double)gSampleRate);
   const double maxBufferedFrames = sourceRate * kMaxBufferedSeconds;
   const double available = (double)writeFrame - mReadFrame;
   if (available > maxBufferedFrames)
      mReadFrame = std::max(0.0, (double)writeFrame - maxBufferedFrames);

   SyncOutputBuffer(kOutputChannels);
   target->GetBuffer()->SetNumActiveChannels(kOutputChannels);
   float* outL = target->GetBuffer()->GetChannel(0);
   float* outR = target->GetBuffer()->GetChannel(1);
   float levelSum = 0.0f;
   for (int i = 0; i < bufferSize; ++i)
   {
      float left = 0.0f;
      float right = 0.0f;
      const uint64_t baseFrame = (uint64_t)std::floor(mReadFrame);
      if (writeFrame > baseFrame + 1)
      {
         const float frac = (float)(mReadFrame - (double)baseFrame);
         const float l0 = ReadSample(baseFrame, 0);
         const float r0 = ReadSample(baseFrame, 1);
         const float l1 = ReadSample(baseFrame + 1, 0);
         const float r1 = ReadSample(baseFrame + 1, 1);
         left = l0 + (l1 - l0) * frac;
         right = r0 + (r1 - r0) * frac;
         mReadFrame += step;
      }
      outL[i] += left;
      outR[i] += right;
      levelSum += std::max(std::abs(left), std::abs(right));
      GetVizBuffer()->Write(left, 0);
      GetVizBuffer()->Write(right, 1);
   }

   mLastAudioLevel = levelSum / std::max(1, bufferSize);
   mSmoothedAudioLevel = mSmoothedAudioLevel * 0.85f + mLastAudioLevel * 0.15f;

   if (mHeader->active == 0)
      SetStatus("writer stopped");
   else
      SetStatus("connected " + ofToString((int)mHeader->sampleRate) + "hz");
#endif
}

void SparkPlayer::DrawModule()
{
   if (Minimized() || !IsVisible())
      return;

   DrawTextNormal("sparkplayer", 5, 32);
   DrawTextNormal(mStatus, 5, 50);
   if (mVisualizerPrevButton != nullptr)
      mVisualizerPrevButton->Draw();
   if (mVisualizerNextButton != nullptr)
      mVisualizerNextButton->Draw();
   if (mSyncTransportCheckbox != nullptr)
      mSyncTransportCheckbox->Draw();
   if (mAutoVisualizerCheckbox != nullptr)
      mAutoVisualizerCheckbox->Draw();
}

bool SparkPlayer::EnsureOpen()
{
#if BESPOKE_WINDOWS
   if (mHeader != nullptr)
      return true;

   const std::wstring wide = ToWide(ToWindowsName(mStreamName));
   mMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wide.c_str());
   if (mMapping == nullptr)
   {
      SetStatus("waiting for shm");
      return false;
   }

   void* view = MapViewOfFile(mMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
   if (view == nullptr)
   {
      CloseHandle(mMapping);
      mMapping = nullptr;
      SetStatus("map failed");
      return false;
   }

   mHeader = reinterpret_cast<Header*>(view);
   if (mHeader->magic != kMagic || mHeader->version != kVersion || mHeader->channels < 1 || mHeader->capacityFrames == 0)
   {
      CloseMapping();
      SetStatus("bad shm header");
      return false;
   }

   mCapacityFrames = mHeader->capacityFrames;
   mChannels = std::max(1u, mHeader->channels);
   mSampleRate = std::max(1u, mHeader->sampleRate);
   mMappedBytes = (size_t)mHeader->headerSize + (size_t)mCapacityFrames * (size_t)mChannels * sizeof(float);
   mSamples = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(view) + mHeader->headerSize);
   mGeneration = mHeader->generation;
   ResetReaderToWriter();
   SetStatus("connected");
   return true;
#elif SPARKPLAYER_POSIX_SHM
   if (mHeader != nullptr)
      return true;

   const std::string name = ToPosixName(mStreamName);
   mFd = shm_open(name.c_str(), O_RDWR, 0);
   if (mFd < 0)
   {
      SetStatus("waiting for shm");
      return false;
   }

   void* headerView = mmap(nullptr, sizeof(Header), PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
   if (headerView == MAP_FAILED)
   {
      close(mFd);
      mFd = -1;
      SetStatus("map failed");
      return false;
   }

   Header* header = reinterpret_cast<Header*>(headerView);
   if (header->magic != kMagic || header->version != kVersion || header->channels < 1 || header->capacityFrames == 0)
   {
      munmap(headerView, sizeof(Header));
      close(mFd);
      mFd = -1;
      SetStatus("bad shm header");
      return false;
   }

   const size_t mappedBytes = (size_t)header->headerSize + (size_t)header->capacityFrames * (size_t)header->channels * sizeof(float);
   munmap(headerView, sizeof(Header));

   void* view = mmap(nullptr, mappedBytes, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
   if (view == MAP_FAILED)
   {
      close(mFd);
      mFd = -1;
      SetStatus("map failed");
      return false;
   }

   mHeader = reinterpret_cast<Header*>(view);
   mCapacityFrames = mHeader->capacityFrames;
   mChannels = std::max(1u, mHeader->channels);
   mSampleRate = std::max(1u, mHeader->sampleRate);
   mMappedBytes = mappedBytes;
   mSamples = reinterpret_cast<const float*>(reinterpret_cast<const uint8_t*>(view) + mHeader->headerSize);
   mGeneration = mHeader->generation;
   ResetReaderToWriter();
   SetStatus("connected");
   return true;
#else
   SetStatus("shared memory unsupported");
   return false;
#endif
}

void SparkPlayer::CloseMapping()
{
#if BESPOKE_WINDOWS
   if (mHeader != nullptr)
   {
      UnmapViewOfFile(mHeader);
      mHeader = nullptr;
      mSamples = nullptr;
      mMappedBytes = 0;
   }
   if (mMapping != nullptr)
   {
      CloseHandle(mMapping);
      mMapping = nullptr;
   }
#elif SPARKPLAYER_POSIX_SHM
   if (mHeader != nullptr)
   {
      munmap(mHeader, mMappedBytes);
      mHeader = nullptr;
      mSamples = nullptr;
      mMappedBytes = 0;
   }
   if (mFd >= 0)
   {
      close(mFd);
      mFd = -1;
   }
#endif
}

void SparkPlayer::ResetReaderToWriter()
{
#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM
   const uint64_t writeFrame = mHeader != nullptr ? mHeader->writeFrame : 0;
   mReadFrame = (double)writeFrame;
#endif
}

const float* SparkPlayer::Samples() const
{
#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM
   return mSamples;
#else
   return nullptr;
#endif
}

float SparkPlayer::ReadSample(uint64_t frame, int channel) const
{
   const float* samples = Samples();
   if (samples == nullptr || mCapacityFrames == 0)
      return 0.0f;
   const uint64_t wrapped = frame % mCapacityFrames;
   const uint32_t channelIndex = (uint32_t)std::clamp(channel, 0, (int)mChannels - 1);
   return samples[(size_t)wrapped * (size_t)mChannels + channelIndex];
}

void SparkPlayer::PublishTransportControl()
{
#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM
   if (!mSyncTransport || TheSynth == nullptr)
      return;
   if (mHeader == nullptr && !EnsureOpen())
      return;

   const bool paused = TheSynth->IsAudioPaused();
   if (!mHaveLastTransportPaused || paused != mLastTransportPaused)
   {
      mHaveLastTransportPaused = true;
      mLastTransportPaused = paused;
      WriteTransportControl(!paused);
   }
#endif
}

void SparkPlayer::PublishAutoVisualizerControl()
{
#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM
   if (!mAutoVisualizer)
      return;
   if (mHeader == nullptr && !EnsureOpen())
      return;

   const double elapsedMs = mLastAutoVisualizerPollTime > 0.0 ? std::max(0.0, gTime - mLastAutoVisualizerPollTime) : 0.0;
   mLastAutoVisualizerPollTime = gTime;
   if (mAutoVisualizerGateLevel > kAutoVisualizerThreshold)
   {
      const float decay = std::pow(0.35f, (float)(elapsedMs / 1000.0));
      mAutoVisualizerGateLevel = kAutoVisualizerThreshold + (mAutoVisualizerGateLevel - kAutoVisualizerThreshold) * decay;
   }

   const float level = std::max(mLastAudioLevel, mSmoothedAudioLevel);
   if (level < kAutoVisualizerThreshold)
   {
      mAutoVisualizerGateLevel = kAutoVisualizerThreshold;
      return;
   }

   if (gTime < mNextAutoVisualizerTime || level < mAutoVisualizerGateLevel)
      return;

   mAutoVisualizerRandomState = mAutoVisualizerRandomState * 1664525u + 1013904223u + (uint32_t)(level * 1000000.0f);
   const double randomAmount = (double)(mAutoVisualizerRandomState & 0xffffu) / 65535.0;
   const float loudness = ofClamp((level - kAutoVisualizerThreshold) / kAutoVisualizerLevelRange, 0.0f, 1.0f);
   const double baseDelayMs = ofLerp(kAutoVisualizerMaxDelayMs, kAutoVisualizerMinDelayMs, loudness);
   const double jitterMs = ofLerp(0.75, 1.35, randomAmount);

   WriteVisualizerControl(1);
   mAutoVisualizerGateLevel = std::max(kAutoVisualizerThreshold, level * kAutoVisualizerGateScale);
   mNextAutoVisualizerTime = gTime + baseDelayMs * jitterMs;
#endif
}
void SparkPlayer::ButtonClicked(ClickButton* button, double time)
{
   if (button == mVisualizerPrevButton)
      WriteVisualizerControl(-1);
   if (button == mVisualizerNextButton)
      WriteVisualizerControl(1);
}

void SparkPlayer::WriteTransportControl(bool shouldPlay)
{
#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM
   if (mHeader == nullptr)
      return;
   ++mTransportControlGeneration;
   mHeader->reserved[1] = shouldPlay ? 1 : 2;
   mHeader->reserved[0] = mTransportControlGeneration;
#endif
}

void SparkPlayer::WriteVisualizerControl(int delta)
{
#if BESPOKE_WINDOWS || SPARKPLAYER_POSIX_SHM
   if (mHeader == nullptr)
      return;
   ++mVisualizerControlGeneration;
   mHeader->reserved[3] = (uint32_t)delta;
   mHeader->reserved[2] = mVisualizerControlGeneration;
#endif
}
void SparkPlayer::SetStatus(const std::string& status)
{
   mStatus = status;
}

void SparkPlayer::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadString("stream", moduleInfo, mStreamName);
   SetUpFromSaveData();
}

void SparkPlayer::SetUpFromSaveData()
{
   mStreamName = mModuleSaveData.GetString("stream");
   if (mStreamName.empty())
      mStreamName = kDefaultStreamName;
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
}