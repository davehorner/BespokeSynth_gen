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
#include "IAudioSource.h"
#include "IDrawableModule.h"
#include "Slider.h"
#include "TextEntry.h"

#include <array>
#include <cstdint>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <vector>

class Acuneus;
class IAudioReceiver;

class CandleVideo : public IAudioSource, public IDrawableModule, public IFloatSliderListener, public IIntSliderListener, public IDropdownListener, public IButtonListener, public ITextEntryListener
{
public:
   CandleVideo();
   ~CandleVideo() override;
   static IDrawableModule* Create() { return new CandleVideo(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Poll() override;
   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsEnabled() const override { return mEnabled; }

   void EnableAutoLoadPatch();

   void ButtonClicked(ClickButton* button, double time) override;
   void TextEntryComplete(TextEntry* entry) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 6; }
   std::vector<IUIControl*> ControlsToIgnoreInSaveState() const override;

private:
   struct GenerationResult
   {
      bool success{ false };
      std::string outputPath;
      std::string output;
      std::string error;
      std::string prompt;
      int seed{ 0 };
      uint32_t crc32{ 0 };
   };

   struct PromptIdeasResult
   {
      bool success{ false };
      std::vector<std::string> prompts;
      std::string source;
      std::string status;
   };

   void DrawModule() override;
   void OnClicked(float x, float y, bool right) override;
   bool MouseScrolled(float x, float y, float scrollX, float scrollY, bool isSmoothScroll, bool isInvertedScroll) override;
   void StartGeneration();
   GenerationResult GenerateToFile(std::string prompt, std::string root, std::string weights, std::string features, std::string version, int width, int height, int frames, int fps, int steps, int seed, bool cpu, std::string outputDir);
   std::string BuildOutputDir() const;
   std::string GetGeneratedVideoDirectory() const;
   std::string GetDefaultRoot() const;
   std::string GetDefaultWeights() const;
   std::string GetDefaultUnifiedWeights() const;
   void RefreshGeneratedVideoListIfInstanceChanged();
   void RefreshGeneratedVideoList();
   void AdvanceAutonextIfNeeded();
   void AdvanceToNextGeneratedVideo();
   void LoadSelectedVideo();
   void LoadVideoIntoTarget(const std::string& path);
   std::vector<Acuneus*> GetTargetAcuneusModules();
   void CollectTargetAcuneusModules(IAudioReceiver* receiver, std::vector<Acuneus*>& acuneusModules, std::set<IAudioReceiver*>& visited);
   void UnloadTargetMediaIfNeeded(const std::vector<std::string>& deletingPaths);
   bool DeleteFileWithRetries(const std::string& path) const;
   void DeleteSelectedGeneratedVideo();
   void DeleteAllGeneratedVideos();
   void UpdateGeneratedVideoDropdown();
   std::string GetGeneratedVideoLabel(const std::string& path) const;
   std::string ReadGeneratedVideoMetadataPrompt(const std::string& path) const;
   double GetVideoDurationSeconds(const std::string& path) const;
   double ProbeVideoDurationWithFfprobe(const std::string& path) const;
   double ProbeVideoDurationWithFfmpeg(const std::string& path) const;
   std::string GetFfprobeExecutable() const;
   uint32_t ComputeFileCrc32(const std::string& path) const;
   bool GeneratedVideoCrcExists(uint32_t crc32, const std::string& ignorePath) const;
   std::string BuildGeneratedVideoFilename(int seed, uint32_t crc32) const;
   void RefreshPromptChoices();
   void GenerateMorePromptIdeas();
   std::string MakeGeneratedPromptIdea();
   static PromptIdeasResult GeneratePromptIdeasAsync(std::string model, std::string prompt, std::string promptCommand);
   static PromptIdeasResult GeneratePromptIdeasFromOllama(std::string model, std::string prompt);
   static PromptIdeasResult GeneratePromptIdeasFromCommand(std::string promptCommand, std::string prompt);
   static std::vector<std::string> ParsePromptCommandOutput(const std::string& output);
   void CompletePromptIdeas(PromptIdeasResult result);
   void UseRandomPromptAndStartGeneration();
   void AutoplayNextPrompt();
   void ScheduleNextAutoplay();
   void AddPromptChoice(const std::string& prompt);
   void SelectPromptChoice(int index);
   void AppendPromptIdeasStatus(const std::string& source, int startIndex);
   void ApplyPromptChoice();
   void SetPromptText(const std::string& prompt);
   void WriteGeneratedVideoMetadata(const GenerationResult& result) const;
   ofRectangle GetStatusRect() const;
   std::string GetStatusText() const;
   std::vector<std::string> GetWrappedStatusLines() const;
   void ClampStatusScroll();
   void SetStatusText(const std::string& text);
   void AppendStatusText(const std::string& text);
   void ScrollStatusToEnd();

   std::future<GenerationResult> mGenerationFuture;
   std::future<PromptIdeasResult> mPromptIdeasFuture;
   bool mGenerationInProgress{ false };
   bool mPromptIdeasInProgress{ false };
   bool mGenerateAfterPromptIdeas{ false };
   std::string mStatusString;
   mutable std::mutex mStatusMutex;
   int mStatusScrollLine{ 0 };
   std::string mGeneratedVideoDirectory;
   std::vector<std::string> mGeneratedVideoPaths;
   std::string mLoadedVideoPath;
   std::vector<std::string> mPromptChoices;
   double mAutoplayNextGenerationTime{ -1 };
   double mAutonextVideoLoadTime{ -1 };

   std::string mPrompt{ "A neon modular synthesizer patch generating glowing waveforms, cinematic, high contrast" };
   std::string mRoot;
   std::string mWeights;
   std::string mCargoFeatures{ "flash-attn" };
   std::string mOllamaModel{ "llama3.2" };
   std::string mPromptCommand;
   std::string mVersion{ "0.9.8-2b-distilled" };
   int mWidthParam{ 512 };
   int mHeightParam{ 512 };
   int mFrames{ 25 };
   int mFps{ 25 };
   int mSteps{ 8 };
   int mSeed{ 0 };
   bool mCpu{ false };
   bool mAutoload{ true };
   bool mAutonext{ false };
   bool mAutoplay{ false };
   bool mUseMetadataVideoLabels{ true };
   int mGeneratedVideoIndex{ -1 };
   int mPromptChoice{ -1 };

   TextEntry* mPromptEntry{ nullptr };
   DropdownList* mPromptDropdown{ nullptr };
   ClickButton* mMoreIdeasButton{ nullptr };
   Checkbox* mAutoplayCheckbox{ nullptr };
   TextEntry* mRootEntry{ nullptr };
   TextEntry* mWeightsEntry{ nullptr };
   TextEntry* mFeaturesEntry{ nullptr };
   TextEntry* mOllamaModelEntry{ nullptr };
   TextEntry* mPromptCommandEntry{ nullptr };
   DropdownList* mGeneratedVideoDropdown{ nullptr };
   ClickButton* mGenerateButton{ nullptr };
   ClickButton* mLoadButton{ nullptr };
   Checkbox* mAutoloadCheckbox{ nullptr };
   Checkbox* mAutonextCheckbox{ nullptr };
   ClickButton* mDeleteVideoButton{ nullptr };
   ClickButton* mDeleteAllVideosButton{ nullptr };
   Checkbox* mUseMetadataVideoLabelsCheckbox{ nullptr };
   Checkbox* mCpuCheckbox{ nullptr };
   IntSlider* mWidthSlider{ nullptr };
   IntSlider* mHeightSlider{ nullptr };
   IntSlider* mFramesSlider{ nullptr };
   IntSlider* mFpsSlider{ nullptr };
   IntSlider* mStepsSlider{ nullptr };
   IntSlider* mSeedSlider{ nullptr };
};
