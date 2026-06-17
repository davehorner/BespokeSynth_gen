/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "DefaultOutputLoopback.h"
#include "IAudioReceiver.h"
#include "ModularSynth.h"
#include "Profiler.h"
#include "UIControlMacros.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#if BESPOKE_WINDOWS
#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <windows.h>
#undef LoadString
#endif

#if BESPOKE_LINUX
#include "juce_core/juce_core.h"
#endif

DefaultOutputLoopback::DefaultOutputLoopback()
: IDrawableModule(150, 42)
{
   mRingFrames = std::max<uint64_t>(uint64_t(gSampleRate) * 4, uint64_t(gBufferSize) * 16);
   mRing.assign(size_t(mRingFrames * 2), 0.0f);
   mProcessBuffer.SetNumActiveChannels(2);
   SetStatus("waiting");
}

DefaultOutputLoopback::~DefaultOutputLoopback()
{
   StopCapture();
}

void DefaultOutputLoopback::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   FLOATSLIDER_DIGITS(mGainSlider, "gain", &mGain, 0.0f, 2.5f, 2);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mMonitorCheckbox, "monitor", &mMonitor);
   ENDUIBLOCK0();
}

void DefaultOutputLoopback::Init()
{
   IDrawableModule::Init();
   mCaptureRequested = true;
}

void DefaultOutputLoopback::Exit()
{
   StopCapture();
   IDrawableModule::Exit();
}

void DefaultOutputLoopback::SetEnabled(bool enabled)
{
   mEnabled = enabled;
   if (enabled)
      mCaptureRequested = true;
}

void DefaultOutputLoopback::Poll()
{
   if (mCaptureRequested && !mCaptureRunning)
   {
      mCaptureRequested = false;
      StartCapture();
   }
}

void DefaultOutputLoopback::StartCapture()
{
   if (!mEnabled || mCaptureRunning)
      return;

   mShouldStop = false;
   mCaptureRunning = true;
   mCaptureThread = std::thread(&DefaultOutputLoopback::CaptureThread, this);
}

void DefaultOutputLoopback::StopCapture()
{
   mShouldStop = true;
   if (mCaptureThread.joinable())
      mCaptureThread.join();
   mCaptureRunning = false;
}

void DefaultOutputLoopback::Process(double time)
{
   PROFILER(DefaultOutputLoopback);

   IAudioReceiver* target = GetTarget();
   if (!mEnabled || !mMonitor || target == nullptr)
      return;

   SyncOutputBuffer(2);

   const int bufferSize = target->GetBuffer()->BufferSize();
   assert(bufferSize == gBufferSize);
   mProcessBuffer.SetNumActiveChannels(2);
   mProcessBuffer.Clear();
   ReadFrames(mProcessBuffer.GetChannel(0), mProcessBuffer.GetChannel(1), bufferSize);

   ChannelBuffer* out = target->GetBuffer();
   for (int ch = 0; ch < 2; ++ch)
   {
      float* buffer = mProcessBuffer.GetChannel(ch);
      for (int i = 0; i < bufferSize; ++i)
      {
         ComputeSliders(i);
         buffer[i] *= mGain;
      }
      Add(out->GetChannel(ch), buffer, bufferSize);
   }

   for (int ch = 0; ch < 2; ++ch)
   {
      mLevelMeterDisplay.Process(ch, mProcessBuffer.GetChannel(ch), bufferSize);
      GetVizBuffer()->WriteChunk(mProcessBuffer.GetChannel(ch), bufferSize, ch);
   }
}

void DefaultOutputLoopback::ReadFrames(float* left, float* right, int frames)
{
   uint64_t read = mReadFrame.load(std::memory_order_relaxed);
   const uint64_t write = mWriteFrame.load(std::memory_order_acquire);

   if (write < read)
      read = write;

   uint64_t available = write - read;
   if (available > mRingFrames)
   {
      read = write - mRingFrames;
      available = mRingFrames;
      mDroppedFrames++;
   }

   const int framesToRead = std::min<int>(frames, int(available));
   const int silenceFrames = frames - framesToRead;
   if (silenceFrames > 0)
   {
      std::fill(left, left + silenceFrames, 0.0f);
      std::fill(right, right + silenceFrames, 0.0f);
      left += silenceFrames;
      right += silenceFrames;
   }

   for (int i = 0; i < framesToRead; ++i)
   {
      const uint64_t ringFrame = (read + uint64_t(i)) % mRingFrames;
      left[i] = mRing[size_t(ringFrame * 2)];
      right[i] = mRing[size_t(ringFrame * 2 + 1)];
   }

   mReadFrame.store(read + uint64_t(framesToRead), std::memory_order_release);
}

void DefaultOutputLoopback::WriteFrame(float left, float right)
{
   uint64_t write = mWriteFrame.load(std::memory_order_relaxed);
   uint64_t read = mReadFrame.load(std::memory_order_acquire);

   if (write - read >= mRingFrames)
   {
      read = write - mRingFrames + 1;
      mReadFrame.store(read, std::memory_order_release);
      mDroppedFrames++;
   }

   const uint64_t ringFrame = write % mRingFrames;
   mRing[size_t(ringFrame * 2)] = left;
   mRing[size_t(ringFrame * 2 + 1)] = right;
   mWriteFrame.store(write + 1, std::memory_order_release);
}

float DefaultOutputLoopback::DecodeSample(const uint8_t* data, int bytesPerSample, bool isFloat) const
{
   if (isFloat)
   {
      if (bytesPerSample == 4)
      {
         float value = 0;
         std::memcpy(&value, data, sizeof(value));
         return value;
      }
      if (bytesPerSample == 8)
      {
         double value = 0;
         std::memcpy(&value, data, sizeof(value));
         return float(value);
      }
   }

   if (bytesPerSample == 2)
   {
      int16_t value = 0;
      std::memcpy(&value, data, sizeof(value));
      return float(value) / 32768.0f;
   }
   if (bytesPerSample == 3)
   {
      int32_t value = (int32_t(data[0]) << 8) | (int32_t(data[1]) << 16) | (int32_t(data[2]) << 24);
      value >>= 8;
      return float(value) / 8388608.0f;
   }
   if (bytesPerSample == 4)
   {
      int32_t value = 0;
      std::memcpy(&value, data, sizeof(value));
      return float(value) / 2147483648.0f;
   }

   return 0.0f;
}

void DefaultOutputLoopback::WriteCapturedFrames(const uint8_t* data, uint32_t frames, const void* formatPtr, bool silent)
{
#if BESPOKE_WINDOWS
   const auto& format = *static_cast<const WAVEFORMATEX*>(formatPtr);
   const int captureRate = int(format.nSamplesPerSec);
   const int channels = std::max<int>(1, int(format.nChannels));
   const int bytesPerSample = std::max<int>(1, int(format.wBitsPerSample / 8));
   bool isFloat = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
   if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
   {
      const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
      isFloat = IsEqualGUID(extensible.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
   }

   mCapturedSampleRate.store(captureRate, std::memory_order_relaxed);
   mCapturedChannels.store(channels, std::memory_order_relaxed);

   double sourcePos = mResamplePosition;
   const double step = double(captureRate) / double(gSampleRate);
   while (sourcePos < frames)
   {
      const uint32_t sourceFrame = std::min<uint32_t>(uint32_t(sourcePos), frames - 1);
      float left = 0.0f;
      float right = 0.0f;
      if (!silent && data != nullptr)
      {
         const uint8_t* frame = data + size_t(sourceFrame) * format.nBlockAlign;
         left = DecodeSample(frame, bytesPerSample, isFloat);
         if (channels > 1)
            right = DecodeSample(frame + bytesPerSample, bytesPerSample, isFloat);
         else
            right = left;
      }
      WriteFrame(left, right);
      sourcePos += step;
   }
   mResamplePosition = sourcePos - frames;
#else
   ignoreUnused(data, frames, format, silent);
#endif
}

void DefaultOutputLoopback::WriteFloatFrames(const float* interleaved, uint32_t frames, int captureRate, int channels)
{
   if (interleaved == nullptr || frames == 0 || captureRate <= 0 || channels <= 0)
      return;

   mCapturedSampleRate.store(captureRate, std::memory_order_relaxed);
   mCapturedChannels.store(channels, std::memory_order_relaxed);

   double sourcePos = mResamplePosition;
   const double step = double(captureRate) / double(gSampleRate);
   while (sourcePos < frames)
   {
      const uint32_t sourceFrame = std::min<uint32_t>(uint32_t(sourcePos), frames - 1);
      const float* frame = interleaved + size_t(sourceFrame) * size_t(channels);
      const float left = frame[0];
      const float right = channels > 1 ? frame[1] : left;
      WriteFrame(left, right);
      sourcePos += step;
   }
   mResamplePosition = sourcePos - frames;
}

void DefaultOutputLoopback::CaptureThread()
{
#if BESPOKE_WINDOWS
   HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
   const bool shouldCoUninitialize = SUCCEEDED(hr);
   if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
   {
      SetStatus("COM init failed");
      mCaptureRunning = false;
      return;
   }

   IMMDeviceEnumerator* enumerator = nullptr;
   IMMDevice* device = nullptr;
   IAudioClient* audioClient = nullptr;
   IAudioCaptureClient* captureClient = nullptr;
   WAVEFORMATEX* mixFormat = nullptr;

   auto cleanup = [&]()
   {
      if (audioClient != nullptr)
         audioClient->Stop();
      if (mixFormat != nullptr)
         CoTaskMemFree(mixFormat);
      if (captureClient != nullptr)
         captureClient->Release();
      if (audioClient != nullptr)
         audioClient->Release();
      if (device != nullptr)
         device->Release();
      if (enumerator != nullptr)
         enumerator->Release();
      if (shouldCoUninitialize)
         CoUninitialize();
      mCaptureRunning = false;
   };

   hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
   if (FAILED(hr))
   {
      SetStatus("device enumerator failed");
      cleanup();
      return;
   }

   hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
   if (FAILED(hr))
   {
      SetStatus("no default output device");
      cleanup();
      return;
   }

   hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
   if (FAILED(hr))
   {
      SetStatus("audio client failed");
      cleanup();
      return;
   }

   hr = audioClient->GetMixFormat(&mixFormat);
   if (FAILED(hr) || mixFormat == nullptr)
   {
      SetStatus("mix format failed");
      cleanup();
      return;
   }

   hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mixFormat, nullptr);
   if (FAILED(hr))
   {
      SetStatus("loopback init failed");
      cleanup();
      return;
   }

   hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
   if (FAILED(hr))
   {
      SetStatus("capture client failed");
      cleanup();
      return;
   }

   hr = audioClient->Start();
   if (FAILED(hr))
   {
      SetStatus("capture start failed");
      cleanup();
      return;
   }

   SetStatus("capturing default output");

   while (!mShouldStop)
   {
      UINT32 packetFrames = 0;
      hr = captureClient->GetNextPacketSize(&packetFrames);
      if (FAILED(hr))
      {
         SetStatus("packet read failed");
         break;
      }

      if (packetFrames == 0)
      {
         Sleep(5);
         continue;
      }

      while (packetFrames > 0)
      {
         BYTE* data = nullptr;
         DWORD flags = 0;
         UINT64 devicePosition = 0;
         UINT64 qpcPosition = 0;
         hr = captureClient->GetBuffer(&data, &packetFrames, &flags, &devicePosition, &qpcPosition);
         if (FAILED(hr))
         {
            SetStatus("buffer read failed");
            break;
         }

         WriteCapturedFrames(data, packetFrames, mixFormat, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
         captureClient->ReleaseBuffer(packetFrames);

         hr = captureClient->GetNextPacketSize(&packetFrames);
         if (FAILED(hr))
         {
            SetStatus("packet read failed");
            break;
         }
      }
   }

   cleanup();
#elif BESPOKE_LINUX
   constexpr int kCaptureChannels = 2;
   const int captureRate = gSampleRate;
   juce::StringArray args;
   args.add("parec");
   args.add("--device=@DEFAULT_MONITOR@");
   args.add("--format=float32le");
   args.add("--rate=" + juce::String(captureRate));
   args.add("--channels=" + juce::String(kCaptureChannels));
   args.add("--latency-msec=20");

   juce::ChildProcess process;
   if (!process.start(args))
   {
      SetStatus("install pipewire-pulse or pulseaudio for default output");
      mCaptureRunning = false;
      return;
   }

   SetStatus("capturing default output monitor");

   std::vector<char> bytes(size_t(gBufferSize) * size_t(kCaptureChannels) * sizeof(float));
   std::vector<char> pending;
   pending.reserve(bytes.size() * 2);

   while (!mShouldStop && process.isRunning())
   {
      const int read = process.readProcessOutput(bytes.data(), int(bytes.size()));
      if (read > 0)
      {
         pending.insert(pending.end(), bytes.begin(), bytes.begin() + read);
         const size_t frameBytes = size_t(kCaptureChannels) * sizeof(float);
         const size_t completeBytes = pending.size() - (pending.size() % frameBytes);
         if (completeBytes > 0)
         {
            std::vector<float> decoded(completeBytes / sizeof(float));
            std::memcpy(decoded.data(), pending.data(), completeBytes);
            WriteFloatFrames(decoded.data(), uint32_t(completeBytes / frameBytes), captureRate, kCaptureChannels);
            pending.erase(pending.begin(), pending.begin() + completeBytes);
         }
         continue;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(5));
   }

   if (process.isRunning())
      process.kill();

   SetStatus(mShouldStop ? "stopped" : "default output capture stopped");
   mCaptureRunning = false;
#elif BESPOKE_MAC
   SetStatus("macOS default-output loopback needs Core Audio tap support");
   while (!mShouldStop)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
   mCaptureRunning = false;
#else
   SetStatus("default output loopback unavailable on this platform");
   while (!mShouldStop)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
   mCaptureRunning = false;
#endif
}

void DefaultOutputLoopback::SetStatus(const std::string& status)
{
   std::lock_guard<std::mutex> lock(mStatusMutex);
   mStatus = status;
}

std::string DefaultOutputLoopback::GetStatus() const
{
   std::lock_guard<std::mutex> lock(mStatusMutex);
   return mStatus;
}

void DefaultOutputLoopback::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mGainSlider->Draw();
   mMonitorCheckbox->Draw();
   mLevelMeterDisplay.Draw(4, 24, 142, 18, mNumChannels);
   DrawTextNormal(GetStatus(), 4, 56, 10);

   if (mDroppedFrames.load(std::memory_order_relaxed) > 0)
      TheSynth->SetNextDrawTooltip("default output loopback dropped frames; reduce load or increase audio buffer size");
}

void DefaultOutputLoopback::GetModuleDimensions(float& width, float& height)
{
   width = 150;
   height = 64;
}

void DefaultOutputLoopback::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   mModuleSaveData.LoadFloat("gain", moduleInfo, 1.0f, 0.0f, 2.5f);
   mModuleSaveData.LoadBool("monitor", moduleInfo, true);
   SetUpFromSaveData();
}

void DefaultOutputLoopback::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
   mGain = mModuleSaveData.GetFloat("gain");
   mMonitor = mModuleSaveData.GetBool("monitor");
   if (mGainSlider != nullptr)
      mGainSlider->SetValue(mGain, gTime, false);
   if (mMonitorCheckbox != nullptr)
      mMonitorCheckbox->SetValue(mMonitor ? 1.0f : 0.0f, gTime, false);
}
