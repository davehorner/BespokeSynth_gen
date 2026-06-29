/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "Awisp.h"
#include "FileStream.h"
#include "SynthGlobals.h"
#include "UIControlMacros.h"
#include "ofxJSONElement.h"

#if BESPOKE_AWISP_ENABLED
#include "awisp_capi.h"
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#ifndef BESPOKE_AWISP_ASSET_ROOT
#define BESPOKE_AWISP_ASSET_ROOT ""
#endif

namespace
{
constexpr int kParamX = 3;
constexpr int kParamRowHeight = 17;
constexpr int kParamColumnGap = 4;
constexpr int kParamStartPadding = 8;

std::string DynamicAwispParamControlName(const std::string& id, const char* suffix = "")
{
   return "awisp_" + id + suffix;
}

std::string AwispComponentSuffix(int index, bool isColor)
{
   if (isColor)
   {
      static const std::array<const char*, 4> kColorSuffixes{ "_r", "_g", "_b", "_a" };
      return kColorSuffixes[(size_t)index];
   }
   static const std::array<const char*, 4> kSuffixes{ "_x", "_y", "_z", "_w" };
   return kSuffixes[(size_t)index];
}
}

Awisp::Awisp()
: IAudioProcessor(gBufferSize)
, IDrawableModule(360, 100)
{
   mAssetRoot = GetAssetRoot();
}

Awisp::~Awisp()
{
   CloseEditor();
   CloseInstance();
}

void Awisp::Init()
{
   IDrawableModule::Init();
   OpenInstance();
}

void Awisp::Exit()
{
   CloseInstance();
   IDrawableModule::Exit();
}

void Awisp::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK(3, 3, 300);
   DROPDOWN(mShaderDropdown, "shader", &mSelectedShader, 150);
   mShaderDropdown->DrawLabel(true);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mEditCheckbox, "edit", &mEdit);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mOpenButton, "open");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mCloseButton, "close");
   UIBLOCK_NEWLINE();
   TEXTENTRY(mShaderEntry, "path", 290, &mCustomShader);
   mShaderEntry->DrawLabel(true);
   TEXTENTRY(mAssetRootEntry, "asset root", 290, &mAssetRoot);
   mAssetRootEntry->DrawLabel(true);
   UIBLOCK_NEWLINE();
   UIBLOCK_PUSHSLIDERWIDTH(70);
   FLOATSLIDER(mXSlider, "x", &mWindowX, -4000, 4000);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mYSlider, "y", &mWindowY, -4000, 4000);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mWidthSlider, "w", &mWindowWidth, 1, 4096);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mHeightSlider, "h", &mWindowHeight, 1, 4096);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_NEWLINE();
   UIBLOCK_PUSHSLIDERWIDTH(90);
   FLOATSLIDER(mTitleBarSlider, "title bar", &mTitleBarVisible, 0, 1);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_SHIFTRIGHT();
   UIBLOCK_PUSHSLIDERWIDTH(80);
   INTSLIDER(mPortSlider, "port", &mRemotePort, 1024, 65535);
   UIBLOCK_POPSLIDERWIDTH();
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mMusicAutomationCheckbox, "music auto", &mMusicAutomation);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mMusicAutomationSlider, "amt", &mMusicAutomationAmount, 0, 1);
   ENDUIBLOCK(mWidth, mHeight);

   mBaseHeight = (int)mHeight + kParamStartPadding;
   mHeight = mBaseHeight + 20;
   RefreshShaderList();
   RefreshParamControls();
}

void Awisp::DrawModule()
{
   PollInstanceStatus();
   PollEditorStatus();

   if (Minimized() || !IsVisible())
      return;

   mShaderDropdown->Draw();
   mEditCheckbox->Draw();
   mOpenButton->Draw();
   mCloseButton->Draw();
   mShaderEntry->Draw();
   mAssetRootEntry->Draw();
   mXSlider->Draw();
   mYSlider->Draw();
   mWidthSlider->Draw();
   mHeightSlider->Draw();
   mTitleBarSlider->Draw();
   mPortSlider->Draw();
   mMusicAutomationCheckbox->Draw();
   mMusicAutomationSlider->Draw();
   for (auto& param : mParams)
   {
      for (auto* slider : param.sliders)
      {
         if (slider != nullptr)
            slider->Draw();
      }
      if (param.intSlider != nullptr)
         param.intSlider->Draw();
      if (param.checkbox != nullptr)
         param.checkbox->Draw();
      if (param.dropdown != nullptr)
         param.dropdown->Draw();
   }
   DrawTextNormal(mStatus, 3, mHeight - 5);
}

void Awisp::RefreshShaderList()
{
   mShaderNames.clear();
   if (mShaderDropdown != nullptr)
      mShaderDropdown->Clear();

#if BESPOKE_AWISP_ENABLED
   const size_t count = awisp_shader_count();
   for (size_t i = 0; i < count; ++i)
   {
      const char* name = awisp_shader_name(i);
      if (name == nullptr || name[0] == '\0')
         continue;
      mShaderNames.push_back(name);
      const char* title = awisp_shader_title(name);
      mShaderDropdown->AddLabel(title != nullptr && title[0] != '\0' ? title : name, (int)mShaderNames.size() - 1);
   }
#endif

   if (mShaderNames.empty())
   {
      mShaderNames.push_back("wisp/test_float.wgsl");
      if (mShaderDropdown != nullptr)
         mShaderDropdown->AddLabel("test float", 0);
   }
   mSelectedShader = std::clamp(mSelectedShader, 0, (int)mShaderNames.size() - 1);
}

void Awisp::ClearParamControls()
{
   for (auto& param : mParams)
   {
      for (auto* slider : param.sliders)
      {
         if (slider != nullptr)
            RemoveUIControl(slider);
      }
      if (param.intSlider != nullptr)
         RemoveUIControl(param.intSlider);
      if (param.checkbox != nullptr)
         RemoveUIControl(param.checkbox);
      if (param.dropdown != nullptr)
         RemoveUIControl(param.dropdown);
   }
   mParams.clear();
   mHeight = mBaseHeight + 20;
}

void Awisp::RefreshParamControls()
{
   ClearParamControls();

#if BESPOKE_AWISP_ENABLED
   const std::string shaderName = GetSelectedShaderName();
   const std::string assetRoot = GetAssetRoot();
   const size_t count = awisp_param_count(assetRoot.c_str(), shaderName.c_str());
   if (count == 0)
      return;

   const int moduleWidth = std::max(260, (int)std::round(mWidth));
   const int groupWidth = moduleWidth - kParamX * 2;
   int y = mBaseHeight;
   mParams.reserve(count);
   for (size_t i = 0; i < count; ++i)
   {
      AwispParamDesc desc{};
      if (!awisp_param_desc(assetRoot.c_str(), shaderName.c_str(), i, &desc) || desc.id == nullptr)
         continue;

      mParams.emplace_back();
      ParamControl& param = mParams.back();
      param.id = desc.id;
      param.label = desc.label != nullptr && desc.label[0] != '\0' ? desc.label : desc.id;
      param.type = (int)desc.param_type;
      param.componentCount = std::clamp((int)desc.component_count, 1, 4);
      param.minValue = desc.min_value;
      param.maxValue = std::max(desc.max_value, desc.min_value + 0.0001f);
      param.step = desc.step;
      param.isColor = desc.is_color;
      for (int component = 0; component < 4; ++component)
      {
         param.defaultValues[(size_t)component] = desc.default_values[component];
         param.values[(size_t)component] = desc.default_values[component];
      }
      param.boolValue = param.values[0] >= 0.5f;
      param.intValue = (int)std::round(param.values[0]);

      if (desc.param_type == AWISP_PARAM_BOOL)
      {
         param.checkbox = new Checkbox(this, DynamicAwispParamControlName(param.id).c_str(), kParamX, y, &param.boolValue);
         y += kParamRowHeight;
      }
      else if ((desc.param_type == AWISP_PARAM_I32 || desc.param_type == AWISP_PARAM_U32) && desc.option_count > 0)
      {
         for (size_t optionIndex = 0; optionIndex < desc.option_count; ++optionIndex)
         {
            const char* label = awisp_param_option_label(assetRoot.c_str(), shaderName.c_str(), i, optionIndex);
            const int value = awisp_param_option_value(assetRoot.c_str(), shaderName.c_str(), i, optionIndex);
            param.options.emplace_back(value, label != nullptr && label[0] != '\0' ? label : std::to_string(value));
         }
         param.dropdown = new DropdownList(this, DynamicAwispParamControlName(param.id).c_str(), kParamX, y, &param.intValue, groupWidth);
         param.dropdown->DrawLabel(true);
         bool hasDefault = false;
         for (const auto& option : param.options)
         {
            param.dropdown->AddLabel(option.second.c_str(), option.first);
            hasDefault |= option.first == param.intValue;
         }
         if (!hasDefault)
            param.dropdown->AddLabel(std::to_string(param.intValue).c_str(), param.intValue);
         y += kParamRowHeight;
      }
      else if (desc.param_type == AWISP_PARAM_I32 || desc.param_type == AWISP_PARAM_U32)
      {
         param.intSlider = new IntSlider(this, DynamicAwispParamControlName(param.id).c_str(), kParamX, y, groupWidth, 15, &param.intValue, (int)std::floor(param.minValue), (int)std::ceil(param.maxValue));
         y += kParamRowHeight;
      }
      else
      {
         const int components = std::clamp(param.componentCount, 1, 4);
         if (components == 1)
         {
            param.sliders[0] = new FloatSlider(this, DynamicAwispParamControlName(param.id).c_str(), kParamX, y, groupWidth, 15, &param.values[0], param.minValue, param.maxValue);
            y += kParamRowHeight;
         }
         else
         {
            const int sliderWidth = std::max(28, (groupWidth - kParamColumnGap * (components - 1)) / components);
            for (int component = 0; component < components; ++component)
            {
               const int x = kParamX + component * (sliderWidth + kParamColumnGap);
               param.sliders[(size_t)component] = new FloatSlider(this, DynamicAwispParamControlName(param.id, AwispComponentSuffix(component, param.isColor).c_str()).c_str(), x, y, sliderWidth, 15, &param.values[(size_t)component], param.minValue, param.maxValue);
            }
            y += kParamRowHeight;
         }
      }

      SetParamControlDisplayNames(param);
      SendParam(param);
   }
   mHeight = std::max(mBaseHeight + 20, y + 20);
#endif
}

void Awisp::SendParam(ParamControl& param)
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr)
      return;

   if (param.type == AWISP_PARAM_BOOL)
      awisp_instance_set_param_bool(mInstance, param.id.c_str(), param.boolValue);
   else if (param.type == AWISP_PARAM_I32)
      awisp_instance_set_param_i32(mInstance, param.id.c_str(), param.intValue);
   else if (param.type == AWISP_PARAM_U32)
      awisp_instance_set_param_u32(mInstance, param.id.c_str(), (uint32_t)std::max(0, param.intValue));
   else
      awisp_instance_set_param_f32(mInstance, param.id.c_str(), param.values.data(), (size_t)std::clamp(param.componentCount, 1, 4));
#endif
}

void Awisp::SetParamControlDisplayNames(ParamControl& param)
{
   const std::string label = param.label.empty() ? param.id : param.label;
   if (param.checkbox != nullptr)
      param.checkbox->SetLabel(label.c_str());
   if (param.dropdown != nullptr)
      param.dropdown->SetOverrideDisplayName(label);
   if (param.intSlider != nullptr)
      param.intSlider->SetOverrideDisplayName(label);
   const int components = std::clamp(param.componentCount, 1, 4);
   for (int component = 0; component < components; ++component)
   {
      if (param.sliders[(size_t)component] == nullptr)
         continue;
      if (components == 1)
         param.sliders[(size_t)component]->SetOverrideDisplayName(label);
      else
      {
         const std::string suffix = AwispComponentSuffix(component, param.isColor).substr(1);
         param.sliders[(size_t)component]->SetOverrideDisplayName(label + " " + suffix);
      }
   }
}

void Awisp::OpenInstance()
{
#if BESPOKE_AWISP_ENABLED
   const std::string shaderName = GetSelectedShaderName();
   if (!mEmbedded)
   {
      SetStatus("external awisp runner not implemented yet");
      return;
   }
   if (mInstance != nullptr)
   {
      if (awisp_instance_load_shader(mInstance, shaderName.c_str()))
      {
         awisp_instance_set_visible(mInstance, true);
         ApplyWindowGeometry();
         ApplyTitleBarVisible();
         ApplyRemotePort();
         RefreshParamControls();
         SetStatus("loaded " + shaderName);
      }
      else
      {
         const char* error = awisp_last_error();
         SetStatus(error != nullptr && error[0] != '\0' ? error : "failed to load awisp shader");
      }
      return;
   }
   mInstance = awisp_instance_open_embedded(mAssetRoot.c_str(), shaderName.c_str(), "Awisp", (int)std::round(mWindowX), (int)std::round(mWindowY), (uint32_t)std::max(1, mWindowWidth), (uint32_t)std::max(1, mWindowHeight));
   if (mInstance == nullptr)
   {
      const char* error = awisp_last_error();
      SetStatus(error != nullptr && error[0] != '\0' ? error : "failed to open awisp");
      return;
   }
   SetStatus("opened " + shaderName);
   ApplyTitleBarVisible();
   ApplyRemotePort();
   RefreshParamControls();
#else
   SetStatus("BespokeSynth was built without Awisp support");
#endif
}

bool Awisp::SetSelectedShaderName(const std::string& shaderName)
{
   RefreshShaderList();
   for (int i = 0; i < (int)mShaderNames.size(); ++i)
   {
      const std::string& candidate = mShaderNames[(size_t)i];
      if (candidate == shaderName || candidate.size() >= shaderName.size() && candidate.compare(candidate.size() - shaderName.size(), shaderName.size(), shaderName) == 0)
      {
         mSelectedShader = i;
         mCustomShader.clear();
         if (mShaderDropdown != nullptr)
            mShaderDropdown->SetValue((float)i, gTime, false);
         if (mShaderEntry != nullptr)
            mShaderEntry->SetText("");
         RefreshParamControls();
         return true;
      }
   }

   mCustomShader = shaderName;
   if (mShaderEntry != nullptr)
      mShaderEntry->SetText(mCustomShader);
   RefreshParamControls();
   return false;
}

void Awisp::EnableMusicAutomation(bool enabled)
{
   mMusicAutomation = enabled;
   if (enabled)
      mMusicAutomationAmount = std::max(mMusicAutomationAmount, 0.65f);
   if (mMusicAutomationCheckbox != nullptr)
      mMusicAutomationCheckbox->SetValue(enabled ? 1.0f : 0.0f, gTime, false);
   if (mMusicAutomationSlider != nullptr)
      mMusicAutomationSlider->SetValue(mMusicAutomationAmount, gTime, false);
}

bool Awisp::SetMediaPath(const std::string& path)
{
   if (path.empty())
      return false;

   if (mMediaFramePaths.empty() || juce::File(path) != juce::File(mMediaFramePaths.front()))
   {
      mMediaFramePaths.clear();
      mMediaFrameIndex = 0;
      mLastMediaFrameTime = -9999.0;
   }

   if (mInstance == nullptr)
      OpenInstance();
   if (mInstance == nullptr)
      return false;

#if BESPOKE_AWISP_ENABLED
   const std::string normalizedPath = juce::File(path).getFullPathName().replace("\\", "/").toStdString();
   if (!awisp_instance_load_image(mInstance, normalizedPath.c_str()))
   {
      const char* error = awisp_last_error();
      SetStatus(error != nullptr && error[0] != '\0' ? error : "failed to load awisp image");
      return false;
   }
   SetStatus("loaded media " + juce::File(normalizedPath).getFileName().toStdString());
   return true;
#else
   SetStatus("BespokeSynth was built without Awisp support");
   return false;
#endif
}

bool Awisp::SetMediaFrames(const std::vector<std::string>& paths, float fps)
{
   if (paths.empty())
      return false;

   mMediaFramePaths = paths;
   mMediaFrameIndex = 0;
   mMediaFrameFps = std::clamp(fps, 1.0f, 60.0f);
   mLastMediaFrameTime = -9999.0;
   return SetMediaPath(mMediaFramePaths.front());
}

void Awisp::UnloadMedia()
{
   mMediaFramePaths.clear();
   mMediaFrameIndex = 0;
   mLastMediaFrameTime = -9999.0;
   SetStatus("media unloaded");
}

void Awisp::Process(double)
{
   AdvanceMediaFrames();
   UpdateAudioAutomationLevel();
   ApplyMusicAutomation();

   if (mInstance != nullptr)
      PushAudioToInstance();

   IAudioReceiver* target = GetTarget();
   if (target != nullptr)
   {
      SyncBuffers();
      for (int ch = 0; ch < GetBuffer()->NumActiveChannels(); ++ch)
      {
         Add(target->GetBuffer()->GetChannel(ch), GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize());
         GetVizBuffer()->WriteChunk(GetBuffer()->GetChannel(ch), GetBuffer()->BufferSize(), ch);
      }
   }

   GetBuffer()->Reset();
}

void Awisp::CloseInstance()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance != nullptr)
   {
      awisp_instance_free(mInstance);
      mInstance = nullptr;
   }
#else
   mInstance = nullptr;
#endif
   ClearParamControls();
}

void Awisp::UpdateAudioAutomationLevel()
{
   ChannelBuffer* buffer = GetBuffer();
   if (buffer == nullptr)
      return;

   double sumSquares = 0.0;
   int sampleCount = 0;
   const int channels = buffer->NumActiveChannels();
   const int frames = buffer->BufferSize();
   for (int ch = 0; ch < channels; ++ch)
   {
      const float* channel = buffer->GetChannel(ch);
      for (int i = 0; i < frames; ++i)
      {
         const float sample = channel[i];
         sumSquares += sample * sample;
         ++sampleCount;
      }
   }

   const float level = sampleCount > 0 ? (float)std::sqrt(sumSquares / sampleCount) : 0.0f;
   mAutomationLevel = mAutomationLevel * 0.82f + level * 0.18f;
}

void Awisp::ApplyMusicAutomation()
{
   if (!mMusicAutomation || mInstance == nullptr || gTime <= mLastAutomationSendTime + 50.0)
      return;

   const float drive = std::clamp(mAutomationLevel * std::clamp(mMusicAutomationAmount, 0.0f, 1.0f) * 4.0f, 0.0f, 1.0f);
   if (drive < 0.01f)
      return;

   mLastAutomationSendTime = gTime;
   const float timeSeconds = (float)gTime * 0.001f;
   for (auto& param : mParams)
   {
      if (param.type == AWISP_PARAM_BOOL || param.type == AWISP_PARAM_I32 || param.type == AWISP_PARAM_U32 || param.sliders[0] == nullptr)
         continue;

      const float span = param.maxValue - param.minValue;
      if (span <= 0.0001f)
         continue;

      unsigned int hash = 2166136261u;
      for (char c : param.id)
         hash = (hash ^ (unsigned char)c) * 16777619u;

      const float phase = (hash % 6283) * 0.001f;
      const float speed = 0.35f + ((hash >> 9) % 100) * 0.008f;
      const float depth = 0.10f + ((hash >> 17) % 100) * 0.003f;
      bool changed = false;
      const int components = std::clamp(param.componentCount, 1, 4);
      for (int component = 0; component < components; ++component)
      {
         if (param.sliders[(size_t)component] == nullptr)
            continue;

         const float componentPhase = phase + component * 2.0943951f;
         const float lfo = std::sin(timeSeconds * speed + componentPhase);
         const float centeredDefault = std::clamp(param.defaultValues[(size_t)component], param.minValue, param.maxValue);
         const float target = std::clamp(centeredDefault + lfo * span * depth * drive, param.minValue, param.maxValue);
         if (std::abs(target - param.values[(size_t)component]) < span * 0.0025f)
            continue;

         param.values[(size_t)component] = target;
         param.sliders[(size_t)component]->SetValue(target, gTime, false);
         changed = true;
      }

      if (changed)
         SendParam(param);
   }
}

void Awisp::OpenEditor()
{
#if BESPOKE_AWISP_ENABLED
   if (mEditorProcess.isRunning())
   {
      SetStatus("editor already open");
      return;
   }

   const std::string editorPath = GetEditorExecutablePath();
   juce::File editorFile(editorPath);
   if (!editorFile.existsAsFile())
   {
      SetStatus("missing wisp editor: " + editorPath);
      mEdit = false;
      if (mEditCheckbox != nullptr)
         mEditCheckbox->SetValue(0.0f, gTime, false);
      return;
   }

   juce::StringArray args;
   args.add(editorFile.getFullPathName());
   if (mEditorProcess.start(args, 0))
      SetStatus("editor open");
   else
   {
      SetStatus("failed to launch wisp editor");
      mEdit = false;
      if (mEditCheckbox != nullptr)
         mEditCheckbox->SetValue(0.0f, gTime, false);
   }
#else
   SetStatus("BespokeSynth was built without Awisp support");
#endif
}

void Awisp::CloseEditor()
{
   if (mEditorProcess.isRunning())
      mEditorProcess.kill();
}

void Awisp::PollEditorStatus()
{
   if (!mEdit)
      return;
   if (!mEditorProcess.isRunning())
   {
      mEdit = false;
      if (mEditCheckbox != nullptr)
         mEditCheckbox->SetValue(0.0f, gTime, false);
      SetStatus("editor closed");
   }
}

std::string Awisp::GetEditorExecutablePath() const
{
#if JUCE_WINDOWS
   constexpr const char* kEditorName = "wisp-editor.exe";
#else
   constexpr const char* kEditorName = "wisp-editor";
#endif
   return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile(kEditorName).getFullPathName().toStdString();
}

void Awisp::ApplyWindowGeometry()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr)
      return;
   awisp_instance_set_geometry(mInstance, (int)std::round(mWindowX), (int)std::round(mWindowY), (uint32_t)std::max(1, mWindowWidth), (uint32_t)std::max(1, mWindowHeight));
#endif
}

void Awisp::ApplyTitleBarVisible()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr)
      return;
   awisp_instance_set_title_bar_visible(mInstance, mTitleBarVisible >= 0.5f);
#endif
}

void Awisp::ApplyRemotePort()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr)
      return;

   if (!awisp_instance_listen_udp(mInstance, (uint16_t)std::clamp(mRemotePort, 0, 65535)))
   {
      const char* error = awisp_last_error();
      SetStatus(error != nullptr && error[0] != '\0' ? error : "failed to open awisp udp port");
      return;
   }
   SetStatus("osc/udp " + std::to_string(mRemotePort));
#endif
}

void Awisp::PushAudioToInstance()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr)
      return;
   const int channels = std::clamp(GetBuffer()->NumActiveChannels(), 1, GetBuffer()->NumTotalChannels());
   const int frames = GetBuffer()->BufferSize();
   if (frames <= 0)
      return;
   mInterleavedAudio.resize((size_t)frames * (size_t)channels);
   for (int i = 0; i < frames; ++i)
   {
      for (int ch = 0; ch < channels; ++ch)
         mInterleavedAudio[(size_t)i * (size_t)channels + (size_t)ch] = GetBuffer()->GetChannel(ch)[i];
   }
   awisp_instance_push_audio(mInstance, mInterleavedAudio.data(), (size_t)frames, (size_t)channels);
#endif
}

void Awisp::AdvanceMediaFrames()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr || mMediaFramePaths.size() <= 1)
      return;

   const double intervalMs = 1000.0 / std::max(1.0f, mMediaFrameFps);
   if (gTime < mLastMediaFrameTime + intervalMs)
      return;

   mLastMediaFrameTime = gTime;
   mMediaFrameIndex = (mMediaFrameIndex + 1) % (int)mMediaFramePaths.size();
   const std::string normalizedPath = juce::File(mMediaFramePaths[(size_t)mMediaFrameIndex]).getFullPathName().replace("\\", "/").toStdString();
   if (!awisp_instance_load_image(mInstance, normalizedPath.c_str()))
   {
      const char* error = awisp_last_error();
      SetStatus(error != nullptr && error[0] != '\0' ? error : "failed to load awisp frame");
      mMediaFramePaths.clear();
   }
#endif
}

void Awisp::PollInstanceStatus()
{
#if BESPOKE_AWISP_ENABLED
   if (mInstance == nullptr)
      return;

   const int status = awisp_instance_status(mInstance);
   if (status == 2)
   {
      CloseInstance();
      SetStatus("awisp exited");
   }
   else if (status == 3)
   {
      const char* error = awisp_instance_error(mInstance);
      const std::string message = error != nullptr && error[0] != '\0' ? error : "awisp embedded app panicked";
      CloseInstance();
      SetStatus(message);
   }
#endif
}

std::string Awisp::GetSelectedShaderName() const
{
   if (!mCustomShader.empty())
      return mCustomShader;
   if (mSelectedShader >= 0 && mSelectedShader < (int)mShaderNames.size())
      return mShaderNames[mSelectedShader];
   return "wisp/test_float.wgsl";
}

std::string Awisp::GetAssetRoot() const
{
   if (!mAssetRoot.empty())
      return mAssetRoot;
   return BESPOKE_AWISP_ASSET_ROOT;
}

void Awisp::SetStatus(const std::string& status)
{
   mStatus = status;
}

void Awisp::ButtonClicked(ClickButton* button, double)
{
   if (button == mOpenButton)
      OpenInstance();
   if (button == mCloseButton)
   {
      if (mInstance != nullptr)
      {
         awisp_instance_set_visible(mInstance, false);
         SetStatus("hidden");
      }
   }
}

void Awisp::DropdownUpdated(DropdownList* list, int oldVal, double)
{
   if (list == mShaderDropdown && oldVal != mSelectedShader)
   {
      RefreshParamControls();
      if (mInstance != nullptr)
         OpenInstance();
      return;
   }

   for (auto& param : mParams)
   {
      if (list == param.dropdown)
      {
         SendParam(param);
         return;
      }
   }
}

void Awisp::FloatSliderUpdated(FloatSlider* slider, float, double)
{
   if (slider == mMusicAutomationSlider)
      return;

   if (slider == mTitleBarSlider)
   {
      ApplyTitleBarVisible();
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

   ApplyWindowGeometry();
}

void Awisp::IntSliderUpdated(IntSlider* slider, int, double)
{
   if (slider == mPortSlider)
   {
      ApplyRemotePort();
      return;
   }

   for (auto& param : mParams)
   {
      if (slider == param.intSlider)
      {
         SendParam(param);
         return;
      }
   }

   ApplyWindowGeometry();
}

void Awisp::TextEntryComplete(TextEntry*)
{
   RefreshParamControls();
   if (mInstance != nullptr)
      OpenInstance();
}

void Awisp::CheckboxUpdated(Checkbox* checkbox, double)
{
   if (checkbox == mEditCheckbox)
   {
      if (mEdit)
         OpenEditor();
      else
         CloseEditor();
      return;
   }

   if (checkbox == mMusicAutomationCheckbox)
   {
      if (mMusicAutomation)
         mMusicAutomationAmount = std::max(mMusicAutomationAmount, 0.65f);
      if (mMusicAutomationSlider != nullptr)
         mMusicAutomationSlider->SetValue(mMusicAutomationAmount, gTime, false);
      return;
   }

   for (auto& param : mParams)
   {
      if (checkbox == param.checkbox)
      {
         SendParam(param);
         return;
      }
   }
}

void Awisp::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadInt("remote_port", moduleInfo, 7941, 1024, 65535);
   SetUpFromSaveData();
}

void Awisp::SaveLayout(ofxJSONElement& moduleInfo)
{
   moduleInfo["remote_port"] = mRemotePort;
}

void Awisp::SetUpFromSaveData()
{
   if (mModuleSaveData.HasProperty("remote_port"))
      mRemotePort = mModuleSaveData.GetInt("remote_port");
   RefreshShaderList();
}

void Awisp::SaveState(FileStreamOut& out)
{
   out << mSelectedShader;
   out << mCustomShader;
   out << mAssetRoot;
   out << mEmbedded;
   out << mWindowX;
   out << mWindowY;
   out << mWindowWidth;
   out << mWindowHeight;
   out << mMusicAutomation;
   out << mMusicAutomationAmount;
   out << mTitleBarVisible;
   out << mRemotePort;
}

void Awisp::LoadState(FileStreamIn& in, int rev)
{
   in >> mSelectedShader;
   in >> mCustomShader;
   in >> mAssetRoot;
   in >> mEmbedded;
   in >> mWindowX;
   in >> mWindowY;
   in >> mWindowWidth;
   in >> mWindowHeight;
   if (rev >= 2)
   {
      in >> mMusicAutomation;
      in >> mMusicAutomationAmount;
   }
   else
   {
      mMusicAutomation = false;
      mMusicAutomationAmount = 0.65f;
   }
   if (rev >= 3)
      in >> mTitleBarVisible;
   else
      mTitleBarVisible = 1.0f;
   if (rev >= 4)
      in >> mRemotePort;
   else
      mRemotePort = 7941;
   mEdit = false;
}
