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
//
//  StableAudio.h
//  StableAudioSynth
//
//  Created by David Horner on 5/24/26.
//
//
#pragma once

#include "Checkbox.h"
#include "ClickButton.h"
#include "DropdownList.h"
#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "INoteReceiver.h"
#include "IPulseReceiver.h"
#include "Slider.h"
#include "TextEntry.h"

#include <future>
#include <mutex>
#include <string>
#include <vector>

class Sample;
struct StableAudioModel;

class StableAudio : public IAudioProcessor, public IDrawableModule, public INoteReceiver, public IFloatSliderListener, public IIntSliderListener, public IDropdownListener, public IButtonListener, public ITextEntryListener, public IPulseReceiver
{
public:
   StableAudio();
   ~StableAudio() override;
   static IDrawableModule* Create() { return new StableAudio(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return true; }

   void CreateUIControls() override;
   void Init() override;
   void Poll() override;
   void EnableAutoGenerationPatch();

   void Process(double time) override;
   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsEnabled() const override { return mEnabled; }

   void PlayNote(NoteMessage note) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void OnPulse(double time, float velocity, int flags) override;

   void ButtonClicked(ClickButton* button, double time) override;
   void TextEntryComplete(TextEntry* entry) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 7; }
   std::vector<IUIControl*> ControlsToIgnoreInSaveState() const override;

private:
   struct GenerationResult
   {
      bool success{ false };
      std::string outputPath;
      std::string error;
      std::string model;
      std::string prompt;
      float seconds{ 0 };
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
   GenerationResult GenerateToFile(std::string prompt, std::string ditPath, std::string decoderPath, std::string textEncoderPath, float seconds, int steps, int seed, std::string outputPath);
   void LoadGeneratedSample(const std::string& path);
   void Trigger(double time, float velocity);
   void FreeModel();
   std::string BuildOutputPath() const;
   std::string GetGeneratedAudioDirectory() const;
   void RefreshGeneratedWavListIfInstanceChanged();
   void RefreshGeneratedWavList();
   void AutoloadSelectedGeneratedWavIfNeeded();
   void AdvanceAutonextIfNeeded();
   void AdvanceToNextGeneratedWav();
   void LoadSelectedGeneratedWav();
   void DeleteSelectedGeneratedWav();
   void DeleteAllGeneratedWavs();
   void UpdatePlaybackControls();
   void RefreshPromptChoices();
   void GenerateMorePromptIdeas();
   static PromptIdeasResult GeneratePromptIdeasFromOllama(std::string model, std::string prompt, std::string transportPrompt);
   void CompletePromptIdeas(PromptIdeasResult result);
   void UseRandomPromptAndStartGeneration();
   std::string MakeGeneratedPromptIdea();
   std::string GetTransportPromptFragment() const;
   bool PromptContainsTransportData(const std::string& prompt) const;
   bool ShouldUseDefaultPrompt() const;
   void AutoplayNextPrompt();
   void ScheduleNextAutoplay();
   void AddPromptChoice(const std::string& prompt);
   void SelectPromptChoice(int index);
   void AppendPromptIdeasStatus(const std::string& source, int startIndex);
   void ApplyPromptChoice();
   void SetPromptText(const std::string& prompt);
   bool TryGetPromptDurationSeconds(const std::string& prompt, float& seconds) const;
   void ApplyPromptDuration();
   bool ShouldUseCrossfadeLoop() const;
   void ApplyLoopCrossfade(double startPlayPosition, int bufferSize);
   bool TryGetPromptBpm(const std::string& prompt, float& bpm) const;
   void SyncTransportToPromptBpm();
   void SyncTransportToPromptBpm(const std::string& prompt);
   std::string GetGeneratedWavLabel(const std::string& path) const;
   std::string ReadGeneratedWavMetadataLabel(const std::string& path) const;
   std::string ReadGeneratedWavMetadataPrompt(const std::string& path) const;
   void WriteGeneratedWavMetadata(const GenerationResult& result) const;
   void AddAvailableModelLabels();
   void ApplyModelSelection();
   void RefreshModelPathEntries();
   std::string ResolveModelPath(const std::string& path) const;
   bool ModelFilesExist(const std::string& ditFilename, const std::string& decoderFilename) const;
   float GetSelectedModelMaxSeconds() const;
   float GetMaxCrossfadeSeconds() const;
   void UpdateCrossfadeSlider();
   const char* GetSelectedModelLabel() const;
   const char* GetSelectedModelDescription() const;
   ofRectangle GetStatusRect() const;
   std::string GetStatusText() const;
   std::vector<std::string> GetWrappedStatusLines() const;
   void ClampStatusScroll();

   // Guard sample pointer swaps and playback reads. Generated WAVs load on the UI thread,
   // while Process() consumes these Sample objects on the audio thread.
   Sample* mSample{ nullptr };
   Sample* mPreviousSample{ nullptr };
   std::mutex mSampleMutex;
   ChannelBuffer mCrossfadeBuffer{ gBufferSize };
   StableAudioModel* mModel{ nullptr };
   std::string mLoadedDitPath;
   std::string mLoadedDecoderPath;
   std::string mLoadedTextEncoderPath;
   int mLoadedSteps{ 0 };
   int mLoadedSeed{ 0 };
   std::vector<std::string> mGeneratedWavPaths;
   std::vector<std::string> mPromptChoices;
   std::string mGeneratedAudioDirectory;
   std::string mCurrentSamplePath;
   std::string mCurrentSamplePrompt;

   std::future<GenerationResult> mGenerationFuture;
   std::future<PromptIdeasResult> mPromptIdeasFuture;
   bool mGenerationInProgress{ false };
   bool mPromptIdeasInProgress{ false };
   bool mGenerateAfterPromptIdeas{ false };
   std::string mStatusString;
   int mStatusScrollLine{ 0 };
   double mAutoplayNextGenerationTime{ -1 };
   double mCrossfadeStartTime{ -1 };
   double mPendingTransportSyncTime{ -1 };
   std::string mPendingTransportSyncPrompt;

   enum ModelSelection
   {
      kModel_SmallMusic,
      kModel_SmallSfx,
      kModel_Medium
   };

   enum TransitionMode
   {
      kTransition_Normal,
      kTransition_Crossfade
   };

   std::string mModelDir;
   int mModelSelection{ kModel_SmallMusic };
   std::string mPrompt;
   std::string mOllamaModel{ "llama3.2" };
   std::string mDitPath;
   std::string mDecoderPath;
   std::string mTextEncoderPath;
   float mSeconds{ 8 };
   int mSteps{ 8 };
   int mSeed{ 0 };
   int mTransitionMode{ kTransition_Crossfade };
   float mCrossfadeSeconds{ 2 };
   float mVolume{ 1 };
   bool mPlay{ false };
   bool mLoop{ true };
   bool mAutoplay{ false };
   bool mAutonext{ false };
   bool mSyncTransport{ true };
   int mGeneratedWavIndex{ -1 };
   int mPromptChoice{ -1 };
   bool mUseMetadataWavLabels{ true };

   TextEntry* mPromptEntry{ nullptr };
   DropdownList* mPromptDropdown{ nullptr };
   ClickButton* mMoreIdeasButton{ nullptr };
   Checkbox* mAutoplayCheckbox{ nullptr };
   TextEntry* mOllamaModelEntry{ nullptr };
   DropdownList* mModelDropdown{ nullptr };
   DropdownList* mGeneratedWavDropdown{ nullptr };
   TextEntry* mDitPathEntry{ nullptr };
   TextEntry* mDecoderPathEntry{ nullptr };
   TextEntry* mTextEncoderPathEntry{ nullptr };
   FloatSlider* mSecondsSlider{ nullptr };
   IntSlider* mStepsSlider{ nullptr };
   IntSlider* mSeedSlider{ nullptr };
   DropdownList* mTransitionDropdown{ nullptr };
   FloatSlider* mCrossfadeSlider{ nullptr };
   FloatSlider* mVolumeSlider{ nullptr };
   Checkbox* mSyncTransportCheckbox{ nullptr };
   ClickButton* mGenerateButton{ nullptr };
   Checkbox* mAutonextCheckbox{ nullptr };
   ClickButton* mDeleteWavButton{ nullptr };
   ClickButton* mDeleteAllWavsButton{ nullptr };
   Checkbox* mUseMetadataWavLabelsCheckbox{ nullptr };
   ClickButton* mPlayButton{ nullptr };
   ClickButton* mStopButton{ nullptr };
   Checkbox* mLoopCheckbox{ nullptr };
};
