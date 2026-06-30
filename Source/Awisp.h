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
#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "TextEntry.h"

#include "juce_core/juce_core.h"

#include <array>
#include <string>
#include <vector>

struct AwispInstance;

class Awisp : public IAudioProcessor, public IDrawableModule, public IButtonListener, public IDropdownListener, public IFloatSliderListener, public IIntSliderListener, public ITextEntryListener
{
public:
   Awisp();
   ~Awisp() override;
   static IDrawableModule* Create() { return new Awisp(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Init() override;
   void Exit() override;
   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsEnabled() const override { return mEnabled; }
   void OpenInstance();
   void EnableMusicAutomation(bool enabled = true);
   bool SetSelectedShaderName(const std::string& shaderName);
   bool SetMediaPath(const std::string& path);
   bool SetMediaFrames(const std::vector<std::string>& paths, float fps);
   void UnloadMedia();

   void ButtonClicked(ClickButton* button, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void TextEntryComplete(TextEntry* entry) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 4; }

private:
   struct ParamControl
   {
      std::string id;
      std::string label;
      int type{ 0 };
      int componentCount{ 1 };
      std::array<float, 4> values{ 0, 0, 0, 0 };
      std::array<float, 4> defaultValues{ 0, 0, 0, 0 };
      float minValue{ 0 };
      float maxValue{ 1 };
      float step{ 0 };
      bool isColor{ false };
      bool boolValue{ false };
      int intValue{ 0 };
      std::vector<std::pair<int, std::string>> options;
      std::array<FloatSlider*, 4> sliders{ nullptr, nullptr, nullptr, nullptr };
      IntSlider* intSlider{ nullptr };
      Checkbox* checkbox{ nullptr };
      DropdownList* dropdown{ nullptr };
   };

   void DrawModule() override;
   void RefreshShaderList();
   void RefreshParamControls();
   void ClearParamControls();
   void SendParam(ParamControl& param);
   void SetParamControlDisplayNames(ParamControl& param);
   void ApplyMusicAutomation();
   void UpdateAudioAutomationLevel();
   void CloseInstance();
   void PollInstanceStatus();
   void ApplyWindowGeometry();
   void ApplyTitleBarVisible();
   void ApplyRemotePort();
   void PushAudioToInstance();
   void AdvanceMediaFrames();
   void OpenExternalInstance();
   void CloseExternalInstance();
   void PollExternalStatus();
   bool SendExternalCommand(const std::string& command) const;
   void OpenEditor();
   void CloseEditor();
   void PollEditorStatus();
   std::string GetRunnerExecutablePath() const;
   std::string GetEditorExecutablePath() const;
   std::string GetSelectedShaderName() const;
   std::string GetAssetRoot() const;
   void SetStatus(const std::string& status);

   AwispInstance* mInstance{ nullptr };
   std::vector<std::string> mShaderNames;
   std::vector<ParamControl> mParams;
   int mSelectedShader{ 0 };
   std::string mCustomShader;
   std::string mAssetRoot;
   bool mEmbedded{ true };
   bool mEdit{ false };
   float mWindowX{ 140.0f };
   float mWindowY{ 140.0f };
   int mWindowWidth{ 800 };
   int mWindowHeight{ 600 };
   float mTitleBarVisible{ 1.0f };
   int mRemotePort{ 7941 };
   std::string mStatus{ "idle" };
   std::vector<std::string> mMediaFramePaths;
   int mMediaFrameIndex{ 0 };
   float mMediaFrameFps{ 24.0f };
   double mLastMediaFrameTime{ -9999.0 };
   std::vector<float> mInterleavedAudio;
   bool mMusicAutomation{ false };
   bool mExternalInstance{ false };
   float mMusicAutomationAmount{ 0.65f };
   float mAutomationLevel{ 0.0f };
   double mLastAutomationSendTime{ -9999.0 };
   int mBaseHeight{ 100 };

   DropdownList* mShaderDropdown{ nullptr };
   TextEntry* mShaderEntry{ nullptr };
   TextEntry* mAssetRootEntry{ nullptr };
   Checkbox* mEditCheckbox{ nullptr };
   ClickButton* mOpenButton{ nullptr };
   ClickButton* mCloseButton{ nullptr };
   FloatSlider* mXSlider{ nullptr };
   FloatSlider* mYSlider{ nullptr };
   IntSlider* mWidthSlider{ nullptr };
   IntSlider* mHeightSlider{ nullptr };
   IntSlider* mPortSlider{ nullptr };
   FloatSlider* mTitleBarSlider{ nullptr };
   Checkbox* mMusicAutomationCheckbox{ nullptr };
   FloatSlider* mMusicAutomationSlider{ nullptr };
   juce::ChildProcess mEditorProcess;
   juce::ChildProcess mExternalProcess;
};
