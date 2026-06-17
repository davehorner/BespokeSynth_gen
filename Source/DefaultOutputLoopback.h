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
#include "LevelMeterDisplay.h"
#include "Slider.h"
#include "Checkbox.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class DefaultOutputLoopback : public IAudioSource, public IDrawableModule, public IFloatSliderListener
{
public:
   DefaultOutputLoopback();
   ~DefaultOutputLoopback() override;
   static IDrawableModule* Create() { return new DefaultOutputLoopback(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void Init() override;
   void Exit() override;
   void Poll() override;
   void Process(double time) override;
   void SetEnabled(bool enabled) override;
   bool IsEnabled() const override { return mEnabled; }

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void CreateUIControls() override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override {}

private:
   void DrawModule() override;
   void GetModuleDimensions(float& width, float& height) override;
   void StartCapture();
   void StopCapture();
   void CaptureThread();
   void WriteCapturedFrames(const uint8_t* data, uint32_t frames, const void* format, bool silent);
   void WriteFloatFrames(const float* interleaved, uint32_t frames, int captureRate, int channels);
   void WriteFrame(float left, float right);
   void ReadFrames(float* left, float* right, int frames);
   float DecodeSample(const uint8_t* data, int bytesPerSample, bool isFloat) const;
   void SetStatus(const std::string& status);
   std::string GetStatus() const;

   std::atomic<bool> mEnabled{ true };
   std::atomic<bool> mShouldStop{ false };
   std::atomic<bool> mCaptureRunning{ false };
   std::atomic<bool> mCaptureRequested{ false };
   std::thread mCaptureThread;
   std::vector<float> mRing;
   uint64_t mRingFrames{ 0 };
   std::atomic<uint64_t> mWriteFrame{ 0 };
   std::atomic<uint64_t> mReadFrame{ 0 };
   std::atomic<int> mDroppedFrames{ 0 };
   std::atomic<int> mCapturedSampleRate{ 0 };
   std::atomic<int> mCapturedChannels{ 0 };
   double mResamplePosition{ 0.0 };
   std::string mStatus;
   mutable std::mutex mStatusMutex;
   ChannelBuffer mProcessBuffer{ gBufferSize };
   LevelMeterDisplay mLevelMeterDisplay;
   int mNumChannels{ 2 };
   float mGain{ 1.0f };
   bool mMonitor{ true };
   FloatSlider* mGainSlider{ nullptr };
   Checkbox* mMonitorCheckbox{ nullptr };
};
