/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#pragma once

#include "IAudioSource.h"
#include "IDrawableModule.h"
#include "Checkbox.h"
#include "ClickButton.h"

#include <string>

#if BESPOKE_WINDOWS
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#define SPARKPLAYER_POSIX_SHM 1
#endif

class SparkPlayer : public IAudioSource, public IDrawableModule, public IButtonListener
{
public:
   SparkPlayer();
   ~SparkPlayer() override;
   static IDrawableModule* Create() { return new SparkPlayer(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Poll() override;
   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsEnabled() const override { return mEnabled; }

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void ButtonClicked(ClickButton* button, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override {}

private:
   struct Header
   {
      uint32_t magic;
      uint32_t version;
      uint32_t headerSize;
      uint32_t capacityFrames;
      uint32_t channels;
      uint32_t sampleRate;
      uint64_t writeFrame;
      uint64_t totalFrames;
      uint64_t generation;
      uint32_t active;
      uint32_t reserved[7];
   };

   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override
   {
      width = 170;
      height = 54;
   }

   bool EnsureOpen();
   void CloseMapping();
   void ResetReaderToWriter();
   const float* Samples() const;
   float ReadSample(uint64_t frame, int channel) const;
   void SetStatus(const std::string& status);
   void PublishTransportControl();
   void WriteTransportControl(bool shouldPlay);
   void WriteVisualizerControl(int delta);

static constexpr const char* kDefaultStreamName = "/sparkplayer_audio";

   std::string mStreamName{ kDefaultStreamName };
   std::string mStatus{ "waiting" };
   double mReadFrame{ 0.0 };
   uint64_t mGeneration{ 0 };
   uint64_t mLastWriteFrame{ 0 };
   uint32_t mSampleRate{ 48000 };
   uint32_t mCapacityFrames{ 0 };
   uint32_t mChannels{ 2 };
   uint32_t mTransportControlGeneration{ 0 };
   uint32_t mVisualizerControlGeneration{ 0 };
   bool mHaveLastTransportPaused{ false };
   bool mLastTransportPaused{ false };
   bool mSyncTransport{ true };
   ClickButton* mVisualizerPrevButton{ nullptr };
   ClickButton* mVisualizerNextButton{ nullptr };
   Checkbox* mSyncTransportCheckbox{ nullptr };
   size_t mMappedBytes{ 0 };

#if BESPOKE_WINDOWS
   HANDLE mMapping{ nullptr };
   Header* mHeader{ nullptr };
   const float* mSamples{ nullptr };
#elif SPARKPLAYER_POSIX_SHM
   int mFd{ -1 };
   Header* mHeader{ nullptr };
   const float* mSamples{ nullptr };
#endif
};