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

#pragma once

#include "ClickButton.h"
#include "Checkbox.h"
#include "DropdownList.h"
#include "IAudioProcessor.h"
#include "IDrawableModule.h"
#include "INoteReceiver.h"
#include "IPulseReceiver.h"
#include "Slider.h"
#include "TextEntry.h"

#include "juce_osc/juce_osc.h"

#include <array>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct CuneusInstance;

class Acuneus : public IDrawableModule, public IAudioProcessor, public IFloatSliderListener, public IIntSliderListener, public IDropdownListener, public IButtonListener, public ITextEntryListener, public IPulseReceiver, public INoteReceiver, private juce::OSCReceiver, private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
   Acuneus();
   ~Acuneus() override;
   static IDrawableModule* Create() { return new Acuneus(); }
   static bool AcceptsAudio() { return true; }
   static bool AcceptsNotes() { return true; }
   static bool AcceptsPulses() { return true; }
   static bool AnyMusicAutomationEnabled() { return sMusicAutomationUsers.load() > 0; }

   void CreateUIControls() override;
   void Init() override;
   void Exit() override;
   void Poll() override;
   void Process(double time) override;
   void OpenInstance();
   bool IsInstanceOpen() const { return mInstance != nullptr; }
   void EnableMusicAutomation(bool enabled = true);
   bool SetSelectedBinName(const std::string& binName);
   void SetMediaPath(const std::string& path);
   void UnloadMedia();

   void SetEnabled(bool enabled) override { mEnabled = enabled; }
   bool IsEnabled() const override { return mEnabled; }

   void PlayNote(NoteMessage note) override;
   void SendCC(int control, int value, int voiceIdx = -1) override {}
   void OnPulse(double time, float velocity, int flags) override;

   void FloatSliderUpdated(FloatSlider* slider, float oldVal, double time) override;
   bool FloatSliderContextMenu(FloatSlider* slider) override;
   void IntSliderUpdated(IntSlider* slider, int oldVal, double time) override;
   void DropdownUpdated(DropdownList* list, int oldVal, double time) override;
   void CheckboxUpdated(Checkbox* checkbox, double time) override;
   void ButtonClicked(ClickButton* button, double time) override;
   void TextEntryComplete(TextEntry* entry) override;
   void oscMessageReceived(const juce::OSCMessage& msg) override;

   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SaveLayout(ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;
   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 12; }
   std::vector<IUIControl*> ControlsToIgnoreInSaveState() const override;

private:
   struct ParamControl
   {
      std::string id;
      std::string label;
      std::string group;
      int type{ 0 };
      std::array<float, 3> values{ 0, 0, 0 };
      std::array<float, 3> defaultValues{ 0, 0, 0 };
      float minValue{ 0 };
      float maxValue{ 1 };
      bool boolValue{ false };
      int selectValue{ 0 };
      std::string stringValue;
      std::vector<std::pair<int, std::string>> selectOptions;
      std::array<FloatSlider*, 3> sliders{ nullptr, nullptr, nullptr };
      int musicAutomationMode{ 0 };
      TextEntry* textEntry{ nullptr };
      ClickButton* button{ nullptr };
      Checkbox* checkbox{ nullptr };
      DropdownList* dropdown{ nullptr };
   };

   struct WindowState
   {
      float x{ 100.0f };
      float y{ 100.0f };
      float scale{ 1.0f };
      int baseWidth{ 800 };
      int baseHeight{ 600 };
      float windowWidth{ 800.0f };
      float windowHeight{ 600.0f };
      float resolutionWidth{ 800.0f };
      float resolutionHeight{ 600.0f };
      float anchorOffsetX{ 0.0f };
      float anchorOffsetY{ 0.0f };
      bool hasAnchorOffset{ false };
   };

   struct ParamGroupHeader
   {
      std::string label;
      int x{ 0 };
      int y{ 0 };
   };

   void DrawModule() override;
   void CloseInstance();
   void RefreshBinList();
   void RefreshParamControls();
   void ClearParamControls();
   void SendParam(ParamControl& param);
   void LoadMediaFile(ParamControl* param);
   void ApplyOverlayVisible();
   void ApplyTitleBarVisible();
   void ApplyWindowPosition();
   void CaptureCurrentWindowPosition();
   void ApplyWindowScale(bool updateWindowSizeFromScale = false);
   void ApplyWindowSize();
   void ApplyAnchorWindow();
   void ApplyTime();
   void ApplyFps();
   void ApplyResolution();
   void ApplyMusicAutomation();
   void SetMusicAutomationEnabled(bool enabled);
   void UpdateMusicAutomationFromSample(float sample, float& sumSquares, float& peak, int& sampleCount) const;
   void PublishMusicAutomationLevel(float level);
   void UpdateTrackedMouseParams();
   bool WantsAudioSpectrum() const;
   void UpdateAudioSpectrumFromBuffer(ChannelBuffer* buffer, int channels);
   void SendAudioSpectrum();
   void ShowSliderAutomationMenu(FloatSlider* slider);
   void HideSliderAutomationMenu();
   void SetParamMusicAutomation(FloatSlider* slider, int mode);
   void PushPcmFeedback(const juce::OSCMessage& msg);
   void PushAudioSpectrumFeedback(const juce::OSCMessage& msg);
   bool WriteQueuedPcmToTarget(IAudioReceiver* target, int bufferSize);
   void ReserveRemotePort(int preferredPort);
   void ReleaseRemotePort();
   void UpdateModuleWidthForColumns();
   void SaveSelectedWindowState();
   void LoadSelectedWindowState();
   WindowState DefaultWindowStateForBin(const std::string& binName) const;
   void UpdateTitleBarButtonLabel();
   bool StartFeedbackReceiver();
   void StopFeedbackReceiver();
   void RequestDiscovery();
   void SendTransport();
   void ApplyFeedbackValue(const std::string& id, const juce::OSCMessage& msg);
   void ApplyParamDescFeedback(const juce::OSCMessage& msg);
   bool ApplyTransportFeedback(const std::string& address, const juce::OSCMessage& msg);
   void SetParamSliderDisplayNames(ParamControl& param);
   void SendAction(ParamControl& param, float value);
   bool CalculateAnchoredWindowPosition(float& x, float& y, bool includeSavedOffset);
   std::string GetSelectedBinName() const;
   std::string GetDefaultTitleForBin(const std::string& binName) const;
   std::string GetDefaultExecutableDir() const;
   void SetStatus(std::string status);
   void ShowErrorDialog(const std::string& title, const std::string& error);

   CuneusInstance* mInstance{ nullptr };
   std::vector<std::string> mBinNames;
   std::vector<ParamControl> mParams;
   std::map<std::string, WindowState> mWindowStates;
   std::vector<ParamGroupHeader> mParamGroupHeaders;
   std::string mExecutableDir;
   std::string mWindowTitle{ "Acuneus" };
   std::string mOpenBinName;
   std::string mLoadedMediaPath;
   std::string mStatus;
   int mSelectedBin{ 0 };
   int mParamColumns{ 2 };
   int mRemotePort{ 7841 };
   int mFeedbackPort{ 7842 };
   int mReservedRemotePort{ 0 };
   int mReservedFeedbackPort{ 0 };
   bool mEmbedded{ true };
   bool mAnchorWindow{ true };
   float mOverlayVisible{ 1.0f };
   float mTitleBarVisible{ 1.0f };
   float mWindowX{ 100.0f };
   float mWindowY{ 100.0f };
   float mAnchorOffsetX{ 0.0f };
   float mAnchorOffsetY{ 0.0f };
   float mWindowScale{ 1.0f };
   float mWindowWidth{ 800.0f };
   float mWindowHeight{ 600.0f };
   float mTime{ 0.0f };
   float mFps{ 60.0f };
   float mResolutionWidth{ 800.0f };
   float mResolutionHeight{ 600.0f };
   bool mMusicAutomation{ false };
   bool mMusicAutomationRegistered{ false };
   float mMusicAutomationAmount{ 0.65f };
   std::atomic<float> mMusicAutomationLevel{ 0.0f };
   std::array<float, 69> mAudioSpectrum{};
   std::mutex mAudioSpectrumMutex;
   std::deque<float> mPcmQueue;
   std::mutex mPcmQueueMutex;
   double mLastMusicAutomationSendTime{ -9999 };
   double mLastAudioSpectrumSendTime{ -9999 };
   double mLastMouseSendTime{ -9999 };
   bool mHasAudioSpectrum{ false };
   int mWindowBaseWidth{ 800 };
   int mWindowBaseHeight{ 600 };
   bool mFeedbackReceiverConnected{ false };
   bool mApplyingFeedback{ false };
   int mPendingDiscoveryRequests{ 0 };
   int mPendingAnchorApplies{ 0 };
   int mPendingChromeApplies{ 0 };
   double mLastDiscoveryRequestTime{ -9999 };
   double mLastTransportSendTime{ -9999 };

   DropdownList* mBinDropdown{ nullptr };
   DropdownList* mParamColumnsDropdown{ nullptr };
   TextEntry* mExecutableDirEntry{ nullptr };
   Checkbox* mEmbeddedCheckbox{ nullptr };
   Checkbox* mAnchorCheckbox{ nullptr };
   IntSlider* mPortSlider{ nullptr };
   ClickButton* mOpenButton{ nullptr };
   ClickButton* mCloseButton{ nullptr };
   ClickButton* mToggleOverlayButton{ nullptr };
   ClickButton* mHideTitleBarButton{ nullptr };
   ClickButton* mSetTitleButton{ nullptr };
   TextEntry* mWindowTitleEntry{ nullptr };
   FloatSlider* mOverlaySlider{ nullptr };
   FloatSlider* mTitleBarSlider{ nullptr };
   FloatSlider* mWindowXSlider{ nullptr };
   FloatSlider* mWindowYSlider{ nullptr };
   FloatSlider* mWindowWidthSlider{ nullptr };
   FloatSlider* mWindowHeightSlider{ nullptr };
   FloatSlider* mWindowScaleSlider{ nullptr };
   FloatSlider* mTimeSlider{ nullptr };
   FloatSlider* mFpsSlider{ nullptr };
   FloatSlider* mResolutionWidthSlider{ nullptr };
   FloatSlider* mResolutionHeightSlider{ nullptr };
   Checkbox* mMusicAutomationCheckbox{ nullptr };
   FloatSlider* mMusicAutomationSlider{ nullptr };
   ClickButton* mSliderMenuLfoButton{ nullptr };
   ClickButton* mSliderMenuLooseButton{ nullptr };
   ClickButton* mSliderMenuStrictButton{ nullptr };
   ClickButton* mSliderMenuOffButton{ nullptr };
   FloatSlider* mSliderMenuTarget{ nullptr };

   static std::atomic<int> sMusicAutomationUsers;
   static std::atomic<float> sSharedMusicAutomationLevel;
};
