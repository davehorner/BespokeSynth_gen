/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "CandleVideo.h"
#include "Acuneus.h"
#include "FileStream.h"
#include "IAudioReceiver.h"
#include "ModularSynth.h"
#include "OllamaPromptGenerator.h"
#include "Profiler.h"
#include "SynthGlobals.h"
#include "UIControlMacros.h"
#include "UserPrefs.h"

#include "juce_core/juce_core.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <random>
#include <sstream>

namespace
{
std::string FormatCommandLine(const juce::StringArray& args);
}

CandleVideo::CandleVideo()
: IDrawableModule(620, 260)
{
   mRoot = GetDefaultRoot();
   mWeights = GetDefaultWeights();
}

CandleVideo::~CandleVideo()
{
   if (mPromptIdeasFuture.valid())
      mPromptIdeasFuture.wait();
   if (mGenerationFuture.valid())
      mGenerationFuture.wait();
}

void CandleVideo::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   DROPDOWN(mGeneratedVideoDropdown, "video", &mGeneratedVideoIndex, 250);
   mGeneratedVideoDropdown->DrawLabel(true);
   mGeneratedVideoDropdown->SetUnknownItemString("no generated videos");
   RefreshGeneratedVideoList();
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mAutonextCheckbox, "autonext", &mAutonext);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoadButton, "load");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mDeleteVideoButton, "del");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mDeleteAllVideosButton, "del-all");
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mUseMetadataVideoLabelsCheckbox, "details", &mUseMetadataVideoLabels);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mAutoloadCheckbox, "autoload", &mAutoload);
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mCpuCheckbox, "cpu", &mCpu);
   UIBLOCK_NEWLINE();
   TEXTENTRY(mPromptEntry, "prompt", 70, &mPrompt);
   mPromptEntry->DrawLabel(true);
   UIBLOCK_NEWLINE();
   DROPDOWN(mPromptDropdown, "ideas", &mPromptChoice, 300);
   mPromptDropdown->DrawLabel(true);
   mPromptDropdown->SetUnknownItemString("choose prompt");
   RefreshPromptChoices();
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mMoreIdeasButton, "more ideas...");
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mAutoplayCheckbox, "auto gen", &mAutoplay);
   UIBLOCK_NEWLINE();
   BUTTON(mGenerateButton, "generate");
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mWidthSlider, "width", &mWidthParam, 256, 1024);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mHeightSlider, "height", &mHeightParam, 256, 1024);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mFramesSlider, "frames", &mFrames, 1, 97);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mFpsSlider, "fps", &mFps, 1, 60);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mStepsSlider, "steps", &mSteps, 1, 80);
   UIBLOCK_SHIFTRIGHT();
   INTSLIDER(mSeedSlider, "seed", &mSeed, 0, INT_MAX);
   UIBLOCK_NEWLINE();
   TEXTENTRY(mRootEntry, "root", 64, &mRoot);
   mRootEntry->DrawLabel(true);
   UIBLOCK_NEWLINE();
   TEXTENTRY(mWeightsEntry, "weights", 64, &mWeights);
   mWeightsEntry->DrawLabel(true);
   UIBLOCK_NEWLINE();
   TEXTENTRY(mFeaturesEntry, "features", 32, &mCargoFeatures);
   mFeaturesEntry->DrawLabel(true);
   UIBLOCK_NEWLINE();
   TEXTENTRY(mOllamaModelEntry, "ollama", 24, &mOllamaModel);
   mOllamaModelEntry->DrawLabel(true);
   UIBLOCK_SHIFTRIGHT();
   TEXTENTRY(mPromptCommandEntry, "promptcmd", 64, &mPromptCommand);
   mPromptCommandEntry->DrawLabel(true);
   ENDUIBLOCK0();
}

void CandleVideo::EnableAutoLoadPatch()
{
   mAutoload = true;
   mAutoplay = true;
   if (mAutoloadCheckbox != nullptr)
      mAutoloadCheckbox->SetValue(1.0f, gTime, false);
   if (mAutoplayCheckbox != nullptr)
      mAutoplayCheckbox->SetValue(1.0f, gTime, false);
   if (!mGenerationInProgress)
      AutoplayNextPrompt();
}

void CandleVideo::Poll()
{
   IDrawableModule::Poll();
   RefreshGeneratedVideoListIfInstanceChanged();

   if (mGenerationInProgress && mGenerationFuture.valid() &&
       mGenerationFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
   {
      GenerationResult result = mGenerationFuture.get();
      mGenerationInProgress = false;

      if (result.success)
      {
         mSeed = result.seed;
         if (mSeedSlider != nullptr)
            mSeedSlider->SetValue(mSeed, gTime, false);
         WriteGeneratedVideoMetadata(result);
         RefreshGeneratedVideoList();
         RefreshPromptChoices();
         AppendStatusText("\ngenerated " + juce::File(result.outputPath).getFileName().toStdString());
         if (mAutoload)
            LoadVideoIntoTarget(result.outputPath);
         ScheduleNextAutoplay();
      }
      else
      {
         AppendStatusText("\n" + result.error);
         TheSynth->LogEvent("candlevideo: " + result.error, kLogEventType_Error);
         ScheduleNextAutoplay();
      }
   }

   if (mPromptIdeasInProgress && mPromptIdeasFuture.valid() &&
       mPromptIdeasFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
   {
      PromptIdeasResult result = mPromptIdeasFuture.get();
      mPromptIdeasInProgress = false;
      CompletePromptIdeas(std::move(result));
   }

   if (mAutoplay && !mGenerationInProgress && !mPromptIdeasInProgress &&
       mAutoplayNextGenerationTime > 0 && gTime >= mAutoplayNextGenerationTime)
   {
      AutoplayNextPrompt();
   }

   AdvanceAutonextIfNeeded();
}

void CandleVideo::Process(double time)
{
   PROFILER(CandleVideo);
}

void CandleVideo::StartGeneration()
{
   if (mPromptEntry != nullptr)
      mPrompt = mPromptEntry->GetText();

   juce::String prompt(mPrompt);
   prompt = prompt.trim();
   if (prompt.isEmpty())
   {
      SetPromptText(MakeGeneratedPromptIdea());
      prompt = mPrompt;
   }

   if (mGenerationInProgress)
   {
      AppendStatusText("\ngeneration is already running");
      return;
   }

   if (mPromptIdeasInProgress)
   {
      AppendStatusText("\nprompt generator is still running");
      return;
   }

   const std::string outputDir = BuildOutputDir();
   juce::File(outputDir).createDirectory();
   AppendStatusText("\n\ngenerating...\n");
   mGenerationInProgress = true;

   mGenerationFuture = std::async(std::launch::async,
                                  [this,
                                   prompt = mPrompt,
                                   root = mRoot,
                                   weights = mWeights,
                                   features = mCargoFeatures,
                                   version = mVersion,
                                   width = mWidthParam,
                                   height = mHeightParam,
                                   frames = mFrames,
                                   fps = mFps,
                                   steps = mSteps,
                                   seed = mSeed,
                                   cpu = mCpu,
                                   outputDir]
                                  {
                                     return GenerateToFile(prompt, root, weights, features, version, width, height, frames, fps, steps, seed, cpu, outputDir);
                                  });
}

CandleVideo::GenerationResult CandleVideo::GenerateToFile(std::string prompt, std::string root, std::string weights, std::string features, std::string version, int width, int height, int frames, int fps, int steps, int seed, bool cpu, std::string outputDir)
{
   GenerationResult result;
   result.prompt = prompt;

   const juce::File manifest = juce::File(root).getChildFile("Cargo.toml");
   if (!manifest.existsAsFile())
   {
      result.error = "CandleVideo Cargo.toml not found: " + manifest.getFullPathName().toStdString();
      return result;
   }

   const std::string unified = (juce::File(weights).getChildFile("ltxv-2b-0.9.8-distilled.safetensors")).getFullPathName().toStdString();

   constexpr int kMaxDuplicateRetries = 5;
   for (int attempt = 0; attempt < kMaxDuplicateRetries; ++attempt)
   {
      const int attemptSeed = seed + attempt;
      const juce::File tempOutputFile = juce::File(outputDir).getChildFile("video.mp4");
      tempOutputFile.deleteFile();

      juce::StringArray args;
      args.add("cargo");
      args.add("run");
      args.add("--manifest-path");
      args.add(manifest.getFullPathName());
      if (!features.empty())
      {
         args.add("--features");
         args.add(features);
      }
      args.add("--example");
      args.add("ltx-video");
      args.add("--release");
      args.add("--");
      args.add("--local-weights");
      args.add(weights);
      args.add("--ltxv-version");
      args.add(version);
      if (juce::File(unified).existsAsFile())
      {
         args.add("--unified-weights");
         args.add(unified);
      }

      args.add("--prompt");
      args.add(prompt);
      args.add("--width");
      args.add(juce::String(width));
      args.add("--height");
      args.add(juce::String(height));
      args.add("--num-frames");
      args.add(juce::String(frames));
      args.add("--fps");
      args.add(juce::String(std::max(1, fps)));
      args.add("--steps");
      args.add(juce::String(steps));
      args.add("--seed");
      args.add(juce::String(attemptSeed));
      args.add("--output-dir");
      args.add(outputDir);
      args.add("--mp4");
      if (cpu)
         args.add("--cpu");

      AppendStatusText("running candle-video seed " + std::to_string(attemptSeed) + ":\n" + FormatCommandLine(args) + "\n");

      juce::ChildProcess process;
      if (!process.start(args))
      {
         result.error = "failed to start cargo";
         return result;
      }

      char buffer[4096];
      const int64_t startMs = juce::Time::getMillisecondCounterHiRes();
      int64_t lastOutputMs = startMs;
      int64_t lastHeartbeatMs = startMs;
      constexpr int64_t kMaxGenerationMs = 60 * 1000;
      constexpr int64_t kMaxSilentMs = 60 * 1000;
      constexpr int64_t kHeartbeatMs = 5000;
      const auto formatElapsed = [](int64_t elapsedMs)
      {
         const int64_t totalSeconds = std::max<int64_t>(0, elapsedMs / 1000);
         const int64_t minutes = totalSeconds / 60;
         const int64_t seconds = totalSeconds % 60;
         return std::to_string(minutes) + ":" + (seconds < 10 ? "0" : "") + std::to_string(seconds);
      };
      while (process.isRunning())
      {
         const int numRead = process.readProcessOutput(buffer, sizeof(buffer));
         if (numRead > 0)
         {
            lastOutputMs = juce::Time::getMillisecondCounterHiRes();
            lastHeartbeatMs = lastOutputMs;
            const std::string chunk(buffer, buffer + numRead);
            result.output += chunk;
            AppendStatusText(chunk);
         }
         else
         {
            const int64_t nowMs = juce::Time::getMillisecondCounterHiRes();
            if (nowMs - lastHeartbeatMs > kHeartbeatMs)
            {
               AppendStatusText("still running candle-video seed " + std::to_string(attemptSeed) +
                                " (elapsed " + formatElapsed(nowMs - startMs) +
                                ", no output " + formatElapsed(nowMs - lastOutputMs) + ")\n");
               lastHeartbeatMs = nowMs;
            }
            if (nowMs - startMs > kMaxGenerationMs)
            {
               process.kill();
               result.error = "candle-video timed out after 1 minute";
               if (!result.output.empty())
                  result.error += ": " + result.output.substr(result.output.size() > 4000 ? result.output.size() - 4000 : 0);
               return result;
            }
            if (nowMs - lastOutputMs > kMaxSilentMs)
            {
               process.kill();
               result.error = "candle-video produced no output for 1 minute";
               if (!result.output.empty())
                  result.error += ": " + result.output.substr(result.output.size() > 4000 ? result.output.size() - 4000 : 0);
               return result;
            }
            juce::Thread::sleep(50);
         }
      }

      for (;;)
      {
         const int numRead = process.readProcessOutput(buffer, sizeof(buffer));
         if (numRead <= 0)
            break;

         const std::string chunk(buffer, buffer + numRead);
         result.output += chunk;
         AppendStatusText(chunk);
      }

      const int exitCode = (int)process.getExitCode();

      if (!tempOutputFile.existsAsFile())
      {
         if (exitCode != 0)
         {
            result.error = "candle-video exited " + std::to_string(exitCode) + ": " + result.output;
            if (result.error.size() > 2000)
               result.error = result.error.substr(0, 2000);
            return result;
         }

         result.error = "candle-video finished but did not write " + tempOutputFile.getFullPathName().toStdString() + "\n" + result.output;
         return result;
      }

      const uint32_t crc32 = ComputeFileCrc32(tempOutputFile.getFullPathName().toStdString());
      if (GeneratedVideoCrcExists(crc32, tempOutputFile.getFullPathName().toStdString()))
      {
         AppendStatusText("\nduplicate crc32 " + BuildGeneratedVideoFilename(attemptSeed, crc32) + ", retrying with another seed\n");
         tempOutputFile.deleteFile();
         continue;
      }

      const juce::File finalOutputFile = juce::File(outputDir).getChildFile(BuildGeneratedVideoFilename(attemptSeed, crc32));
      finalOutputFile.deleteFile();
      if (!tempOutputFile.moveFileTo(finalOutputFile))
      {
         result.error = "couldn't rename generated video to " + finalOutputFile.getFullPathName().toStdString();
         return result;
      }

      result.success = true;
      result.seed = attemptSeed;
      result.crc32 = crc32;
      result.outputPath = finalOutputFile.getFullPathName().toStdString();
      if (exitCode != 0)
         AppendStatusText("\ncandle-video exited " + std::to_string(exitCode) + " after writing video");
      return result;
   }

   result.error = "generated duplicate videos for " + std::to_string(kMaxDuplicateRetries) + " seeds; try changing the prompt or settings";
   return result;
}

namespace
{
std::string FormatCommandLine(const juce::StringArray& args)
{
   juce::StringArray formatted;
   for (const auto& arg : args)
   {
      const bool needsQuotes = arg.isEmpty() || arg.containsAnyOf(" \t\r\n\"");
      if (!needsQuotes)
      {
         formatted.add(arg);
         continue;
      }

      juce::String escaped = arg;
      escaped = escaped.replace("\\", "\\\\");
      escaped = escaped.replace("\"", "\\\"");
      formatted.add("\"" + escaped + "\"");
   }

   return formatted.joinIntoString(" ").toStdString();
}
}

std::string CandleVideo::BuildOutputDir() const
{
   juce::String filename = "candlevideo_" + juce::String(juce::Time::getCurrentTime().toMilliseconds());
   return GetGeneratedVideoDirectory() + "/" + filename.toStdString();
}

std::string CandleVideo::GetGeneratedVideoDirectory() const
{
   juce::String instanceName = juce::File::createLegalFileName(Name());
   if (instanceName.isEmpty())
      instanceName = "candlevideo";

   return ofToDataPath("candlevideo/" + instanceName.toStdString());
}

std::string CandleVideo::GetDefaultRoot() const
{
   if (juce::File("R:/w/rust/candle-video/Cargo.toml").existsAsFile())
      return "R:/w/rust/candle-video";
   return "R:/w/rust/candle-video";
}

std::string CandleVideo::GetDefaultWeights() const
{
   const std::string defaultWeights = GetDefaultRoot() + "/models/ltx-video";
   if (juce::File(defaultWeights).exists())
      return defaultWeights;
   return defaultWeights;
}

std::string CandleVideo::GetDefaultUnifiedWeights() const
{
   return GetDefaultWeights() + "/ltxv-2b-0.9.8-distilled.safetensors";
}

void CandleVideo::RefreshGeneratedVideoListIfInstanceChanged()
{
   const std::string generatedVideoDirectory = GetGeneratedVideoDirectory();
   if (generatedVideoDirectory == mGeneratedVideoDirectory)
      return;

   mGeneratedVideoDirectory = generatedVideoDirectory;
   RefreshGeneratedVideoList();
}

void CandleVideo::RefreshGeneratedVideoList()
{
   mGeneratedVideoPaths.clear();
   juce::Array<juce::File> files;
   juce::File(GetGeneratedVideoDirectory()).findChildFiles(files, juce::File::findFiles, true, "*.mp4");
   std::sort(files.begin(), files.end(),
             [](const juce::File& lhs, const juce::File& rhs)
             {
                return lhs.getLastModificationTime().toMilliseconds() > rhs.getLastModificationTime().toMilliseconds();
             });

   for (const auto& file : files)
      mGeneratedVideoPaths.push_back(file.getFullPathName().toStdString());

   UpdateGeneratedVideoDropdown();
}

void CandleVideo::UpdateGeneratedVideoDropdown()
{
   if (mGeneratedVideoDropdown == nullptr)
      return;

   mGeneratedVideoDropdown->Clear();
   for (int i = 0; i < (int)mGeneratedVideoPaths.size(); ++i)
      mGeneratedVideoDropdown->AddLabel(GetGeneratedVideoLabel(mGeneratedVideoPaths[i]), i);

   if (mGeneratedVideoPaths.empty())
      mGeneratedVideoIndex = -1;
   else if (mGeneratedVideoIndex < 0 || mGeneratedVideoIndex >= (int)mGeneratedVideoPaths.size())
      mGeneratedVideoIndex = 0;
}

std::string CandleVideo::GetGeneratedVideoLabel(const std::string& path) const
{
   if (!mUseMetadataVideoLabels)
      return juce::File(path).getFileNameWithoutExtension().toStdString();

   std::string prompt = ReadGeneratedVideoMetadataPrompt(path);
   if (prompt.empty())
      return juce::File(path).getFileNameWithoutExtension().toStdString();

   juce::String label(prompt);
   label = label.replaceCharacter('\r', ' ').replaceCharacter('\n', ' ');
   if (label.length() > 80)
      label = label.substring(0, 80) + "...";
   return label.toStdString();
}

std::string CandleVideo::ReadGeneratedVideoMetadataPrompt(const std::string& path) const
{
   juce::File metadataFile = juce::File(path).withFileExtension(".meta");
   if (!metadataFile.existsAsFile())
      return "";

   juce::StringArray lines;
   metadataFile.readLines(lines);
   for (auto line : lines)
   {
      if (line.startsWith("prompt="))
         return line.fromFirstOccurrenceOf("=", false, false).toStdString();
   }

   return "";
}

double CandleVideo::GetVideoDurationSeconds(const std::string& path) const
{
   double durationSeconds = ProbeVideoDurationWithFfprobe(path);
   if (durationSeconds <= 0)
      durationSeconds = ProbeVideoDurationWithFfmpeg(path);
   if (durationSeconds <= 0)
      durationSeconds = std::max(0.1, (double)mFrames / std::max(1, mFps));
   return durationSeconds;
}

double CandleVideo::ProbeVideoDurationWithFfprobe(const std::string& path) const
{
   juce::StringArray args;
   args.add(GetFfprobeExecutable());
   args.add("-v");
   args.add("error");
   args.add("-show_entries");
   args.add("format=duration");
   args.add("-of");
   args.add("default=noprint_wrappers=1:nokey=1");
   args.add(path);

   juce::ChildProcess process;
   if (!process.start(args))
      return -1;

   const juce::String output = process.readAllProcessOutput().trim();
   if (!process.waitForProcessToFinish(5000) || process.getExitCode() != 0)
      return -1;

   const double durationSeconds = output.getDoubleValue();
   return durationSeconds > 0 ? durationSeconds : -1;
}

double CandleVideo::ProbeVideoDurationWithFfmpeg(const std::string& path) const
{
   juce::StringArray args;
   args.add(UserPrefs.ffmpeg_path.Get());
   args.add("-i");
   args.add(path);

   juce::ChildProcess process;
   if (!process.start(args))
      return -1;

   const juce::String output = process.readAllProcessOutput();
   process.waitForProcessToFinish(5000);

   const juce::String marker("Duration:");
   if (!output.contains(marker))
      return -1;

   const juce::String durationText = output.fromFirstOccurrenceOf(marker, false, false)
                                      .upToFirstOccurrenceOf(",", false, false)
                                      .trim();
   juce::StringArray parts;
   parts.addTokens(durationText, ":", "");
   if (parts.size() != 3)
      return -1;

   const double hours = parts[0].getDoubleValue();
   const double minutes = parts[1].getDoubleValue();
   const double seconds = parts[2].getDoubleValue();
   const double durationSeconds = hours * 3600.0 + minutes * 60.0 + seconds;
   return durationSeconds > 0 ? durationSeconds : -1;
}

std::string CandleVideo::GetFfprobeExecutable() const
{
   const juce::File ffmpegFile(UserPrefs.ffmpeg_path.Get());
#if BESPOKE_WINDOWS
   const juce::File sibling = ffmpegFile.getSiblingFile("ffprobe.exe");
   if (sibling.existsAsFile())
      return sibling.getFullPathName().toStdString();
   return "ffprobe.exe";
#else
   const juce::File sibling = ffmpegFile.getSiblingFile("ffprobe");
   if (sibling.existsAsFile())
      return sibling.getFullPathName().toStdString();
   return "ffprobe";
#endif
}

uint32_t CandleVideo::ComputeFileCrc32(const std::string& path) const
{
   juce::File file(path);
   std::unique_ptr<juce::FileInputStream> stream(file.createInputStream());
   if (stream == nullptr || !stream->openedOk())
      return 0;

   uint32_t crc = 0xffffffffu;
   char buffer[16384];
   for (;;)
   {
      const int bytesRead = stream->read(buffer, sizeof(buffer));
      if (bytesRead <= 0)
         break;

      for (int i = 0; i < bytesRead; ++i)
      {
         crc ^= (uint8_t)buffer[i];
         for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
      }
   }

   return crc ^ 0xffffffffu;
}

bool CandleVideo::GeneratedVideoCrcExists(uint32_t crc32, const std::string& ignorePath) const
{
   juce::Array<juce::File> files;
   juce::File(GetGeneratedVideoDirectory()).findChildFiles(files, juce::File::findFiles, true, "*.mp4");
   const juce::File ignoreFile(ignorePath);
   for (const auto& file : files)
   {
      if (file == ignoreFile)
         continue;

      if (ComputeFileCrc32(file.getFullPathName().toStdString()) == crc32)
         return true;
   }

   return false;
}

std::string CandleVideo::BuildGeneratedVideoFilename(int seed, uint32_t crc32) const
{
   char crcText[9]{};
   std::snprintf(crcText, sizeof(crcText), "%08x", crc32);
   return "seed_" + std::to_string(seed) + "_crc32_" + std::string(crcText) + ".mp4";
}

void CandleVideo::AdvanceAutonextIfNeeded()
{
   if (!mAutonext || mGeneratedVideoPaths.empty() || mAutonextVideoLoadTime <= 0)
      return;

   if (gTime < mAutonextVideoLoadTime)
      return;

   if (mGeneratedVideoIndex < 0 || mGeneratedVideoIndex >= (int)mGeneratedVideoPaths.size())
   {
      mGeneratedVideoIndex = 0;
      const juce::File loadedFile(mLoadedVideoPath);
      if (loadedFile.existsAsFile())
      {
         for (int i = 0; i < (int)mGeneratedVideoPaths.size(); ++i)
         {
            if (juce::File(mGeneratedVideoPaths[i]) == loadedFile)
            {
               mGeneratedVideoIndex = i;
               break;
            }
         }
      }
   }

   mGeneratedVideoIndex = (mGeneratedVideoIndex + 1) % (int)mGeneratedVideoPaths.size();
   AdvanceToNextGeneratedVideo();
}

void CandleVideo::AdvanceToNextGeneratedVideo()
{
   if (mGeneratedVideoIndex < 0 || mGeneratedVideoIndex >= (int)mGeneratedVideoPaths.size())
      return;

   LoadSelectedVideo();
}

void CandleVideo::RefreshPromptChoices()
{
   if (mPromptDropdown == nullptr)
      return;

   const std::string selectedPrompt =
   mPromptChoice >= 0 && mPromptChoice < (int)mPromptChoices.size() ? mPromptChoices[mPromptChoice] : "";

   mPromptChoices.clear();
   mPromptDropdown->Clear();
   mPromptDropdown->SetUnknownItemString("choose prompt");
   mPromptChoice = -1;

   juce::Array<juce::File> files;
   juce::File(GetGeneratedVideoDirectory()).findChildFiles(files, juce::File::findFiles, true, "*.meta");
   for (const auto& file : files)
   {
      juce::StringArray lines;
      file.readLines(lines);
      for (auto line : lines)
      {
         if (line.startsWith("prompt="))
         {
            AddPromptChoice(line.fromFirstOccurrenceOf("=", false, false).toStdString());
            break;
         }
      }
   }

   if (mPromptChoices.empty())
      AddPromptChoice(mPrompt);

   for (int i = 0; i < (int)mPromptChoices.size(); ++i)
   {
      if (mPromptChoices[i] == selectedPrompt)
      {
         mPromptChoice = i;
         break;
      }
   }
}

void CandleVideo::GenerateMorePromptIdeas()
{
   if (mPromptDropdown == nullptr)
      return;

   if (mPromptIdeasInProgress)
   {
      AppendStatusText("prompt generator is already running\n");
      return;
   }

   mPromptChoices.clear();
   mPromptDropdown->Clear();
   mPromptDropdown->SetUnknownItemString("choose prompt");
   mPromptChoice = -1;

   juce::String model(mOllamaModel);
   model = model.trim();
   juce::String command(mPromptCommand);
   command = command.trim();
   if (!model.isEmpty() || !command.isEmpty())
   {
      AppendStatusText("running prompt generator...\n");
      mPromptIdeasInProgress = true;
      mPromptIdeasFuture = std::async(std::launch::async,
                                      [modelText = model.toStdString(),
                                       prompt = mPrompt,
                                       commandText = command.toStdString()]
                                      {
                                         return GeneratePromptIdeasAsync(modelText, prompt, commandText);
                                      });
      return;
   }

   CompletePromptIdeas({});
}

std::string CandleVideo::MakeGeneratedPromptIdea()
{
   static std::mt19937 rng{ std::random_device{}() };
   static const std::array<const char*, 10> ideas{
      "cats playing with a ball of yarn in a cozy room",
      "a cat sleeping in a sunny window with gentle camera movement",
      "kittens chasing a red toy ball across a wooden floor",
      "a cozy living room with curtains moving in soft light",
      "colorful paper lanterns swaying at night",
      "dogs playing fetch in a sunny park",
      "a puppy running through tall grass",
      "two dogs splashing in shallow water",
      "a dog looking out a car window on a quiet road",
      "puppies tumbling on a soft blanket"
   };

   std::uniform_int_distribution<int> dist(0, (int)ideas.size() - 1);
   return ideas[dist(rng)];
}

CandleVideo::PromptIdeasResult CandleVideo::GeneratePromptIdeasAsync(std::string model, std::string prompt, std::string promptCommand)
{
   PromptIdeasResult result;
   juce::String modelString(model);
   modelString = modelString.trim();
   if (!modelString.isEmpty())
   {
      result = GeneratePromptIdeasFromOllama(modelString.toStdString(), prompt);
      if (result.success)
         return result;
   }

   juce::String commandString(promptCommand);
   commandString = commandString.trim();
   if (!commandString.isEmpty())
   {
      PromptIdeasResult commandResult = GeneratePromptIdeasFromCommand(commandString.toStdString(), prompt);
      commandResult.status = result.status + commandResult.status;
      return commandResult;
   }

   return result;
}

CandleVideo::PromptIdeasResult CandleVideo::GeneratePromptIdeasFromOllama(std::string model, std::string prompt)
{
   PromptIdeasResult promptResult;
   promptResult.status += "running ollama prompt generator...\n";
   const OllamaPromptGenerator::Result result = OllamaPromptGenerator::GenerateVideoPrompts(model, prompt, 10);
   if (!result.success)
   {
      promptResult.status += result.error + "\n";
      if (!result.output.empty())
         promptResult.status += result.output + "\n";
      return promptResult;
   }

   promptResult.prompts = result.prompts;
   promptResult.source = "ollama";
   promptResult.success = !promptResult.prompts.empty();
   if (!promptResult.success)
      promptResult.status += "ollama returned no prompt ideas\n";
   return promptResult;
}

CandleVideo::PromptIdeasResult CandleVideo::GeneratePromptIdeasFromCommand(std::string promptCommand, std::string prompt)
{
   PromptIdeasResult promptResult;
   juce::String command(promptCommand);
   command = command.trim();
   if (command.isEmpty())
      return promptResult;

   command = command.replace("{prompt}", juce::String(prompt).quoted());
   promptResult.status += "running prompt command...\n";

   juce::ChildProcess process;
   if (!process.start(command))
   {
      promptResult.status += "failed to start prompt command\n";
      return promptResult;
   }

   const std::string output = process.readAllProcessOutput().toStdString();
   const bool finished = process.waitForProcessToFinish(30000);
   if (!finished)
   {
      process.kill();
      promptResult.status += "prompt command timed out\n";
      return promptResult;
   }

   if (process.getExitCode() != 0)
   {
      promptResult.status += "prompt command exited " + std::to_string((int)process.getExitCode()) + "\n" + output + "\n";
      return promptResult;
   }

   const std::vector<std::string> prompts = ParsePromptCommandOutput(output);
   if (prompts.empty())
   {
      promptResult.status += "prompt command returned no prompts\n";
      return promptResult;
   }

   promptResult.prompts = prompts;
   promptResult.source = "command";
   promptResult.success = true;
   return promptResult;
}

void CandleVideo::CompletePromptIdeas(PromptIdeasResult result)
{
   if (!result.status.empty())
      AppendStatusText(result.status);

   int startIndex = (int)mPromptChoices.size();
   if (result.success)
   {
      for (const auto& prompt : result.prompts)
         AddPromptChoice(prompt);

      if ((int)mPromptChoices.size() == startIndex)
      {
         AppendStatusText((result.source.empty() ? "prompt generator" : result.source) + " returned only duplicate prompt ideas\n");
         result.success = false;
      }
      else
      {
         AppendPromptIdeasStatus(result.source.empty() ? "prompt generator" : result.source, startIndex);
      }
   }

   if (!result.success)
   {
      startIndex = (int)mPromptChoices.size();
      for (int i = 0; i < 5; ++i)
         AddPromptChoice(MakeGeneratedPromptIdea());
      AppendPromptIdeasStatus("built-in", startIndex);
   }

   if (mGenerateAfterPromptIdeas)
      UseRandomPromptAndStartGeneration();
   else if (!mPromptChoices.empty())
      SelectPromptChoice(0);
}

std::vector<std::string> CandleVideo::ParsePromptCommandOutput(const std::string& output)
{
   std::vector<std::string> prompts;
   std::stringstream lines(output);
   std::string line;

   while (std::getline(lines, line))
   {
      juce::String prompt(line);
      prompt = prompt.trim().trimCharactersAtStart("-*0123456789.) \t").trim().unquoted();
      if (prompt.isEmpty())
         continue;
      if (prompt.startsWithIgnoreCase("prompt:"))
         prompt = prompt.fromFirstOccurrenceOf(":", false, false).trim();
      if (prompt.length() > 240)
         prompt = prompt.substring(0, 240);

      const std::string promptText = prompt.toStdString();
      if (std::find(prompts.begin(), prompts.end(), promptText) == prompts.end())
         prompts.push_back(promptText);
      if (prompts.size() >= 10)
         break;
   }

   return prompts;
}

void CandleVideo::AutoplayNextPrompt()
{
   if (!mAutoplay)
      return;

   if (mGenerationInProgress)
   {
      AppendStatusText("\nauto gen: generation is already running");
      return;
   }

   if (mPromptIdeasInProgress)
   {
      AppendStatusText("\nauto gen: prompt generator is already running");
      return;
   }

   mGenerateAfterPromptIdeas = true;
   GenerateMorePromptIdeas();
   if (!mPromptIdeasInProgress)
      UseRandomPromptAndStartGeneration();
}

void CandleVideo::UseRandomPromptAndStartGeneration()
{
   mGenerateAfterPromptIdeas = false;
   if (mGenerationInProgress)
   {
      AppendStatusText("\nauto gen: generation is already running");
      return;
   }

   if (mPromptChoices.empty())
      AddPromptChoice(mPrompt.empty() ? MakeGeneratedPromptIdea() : mPrompt);

   static std::mt19937 rng{ std::random_device{}() };
   std::uniform_int_distribution<int> promptDist(0, (int)mPromptChoices.size() - 1);
   mPromptChoice = promptDist(rng);
   ApplyPromptChoice();
   StartGeneration();
}

void CandleVideo::ScheduleNextAutoplay()
{
   if (!mAutoplay)
   {
      mAutoplayNextGenerationTime = -1;
      return;
   }

   static std::mt19937 rng{ std::random_device{}() };
   std::uniform_real_distribution<float> waitDist(1.0f, 4.0f);
   mAutoplayNextGenerationTime = gTime + waitDist(rng) * 1000.0f;
}

void CandleVideo::AddPromptChoice(const std::string& prompt)
{
   if (prompt.empty())
      return;

   if (std::find(mPromptChoices.begin(), mPromptChoices.end(), prompt) != mPromptChoices.end())
      return;

   const int index = (int)mPromptChoices.size();
   mPromptChoices.push_back(prompt);

   juce::String label(prompt);
   label = label.replaceCharacter('\r', ' ').replaceCharacter('\n', ' ');
   if (label.length() > 70)
      label = label.substring(0, 70) + "...";
   mPromptDropdown->AddLabel(label.toStdString(), index);
}

void CandleVideo::SelectPromptChoice(int index)
{
   if (index < 0 || index >= (int)mPromptChoices.size())
      return;

   if (mPromptDropdown != nullptr)
      mPromptDropdown->SetValue(index, gTime, false);
   else
   {
      mPromptChoice = index;
      ApplyPromptChoice();
   }
}

void CandleVideo::AppendPromptIdeasStatus(const std::string& source, int startIndex)
{
   startIndex = std::clamp(startIndex, 0, (int)mPromptChoices.size());
   const int addedCount = (int)mPromptChoices.size() - startIndex;
   if (addedCount <= 0)
      return;

   AppendStatusText("generated " + ofToString(addedCount) + " prompt ideas from " + source + ":\n");
   for (int i = startIndex; i < (int)mPromptChoices.size(); ++i)
      AppendStatusText(ofToString(i - startIndex + 1) + ". " + mPromptChoices[i] + "\n");
}

void CandleVideo::ApplyPromptChoice()
{
   if (mPromptChoice < 0 || mPromptChoice >= (int)mPromptChoices.size())
      return;

   SetPromptText(mPromptChoices[mPromptChoice]);
}

void CandleVideo::SetPromptText(const std::string& prompt)
{
   mPrompt = prompt;
   if (mPromptEntry != nullptr)
   {
      mPromptEntry->SetText(mPrompt);
      mPromptEntry->UpdateDisplayString();
   }
}

void CandleVideo::WriteGeneratedVideoMetadata(const GenerationResult& result) const
{
   if (!result.success || result.outputPath.empty())
      return;

   juce::String prompt(result.prompt);
   prompt = prompt.replaceCharacter('\r', ' ').replaceCharacter('\n', ' ');

   juce::File(result.outputPath)
   .withFileExtension(".meta")
   .replaceWithText("prompt=" + prompt + "\n" +
                    "seed=" + juce::String(result.seed) + "\n" +
                    "crc32=" + juce::String(BuildGeneratedVideoFilename(result.seed, result.crc32)).fromLastOccurrenceOf("crc32_", false, false).upToLastOccurrenceOf(".mp4", false, false) + "\n");
}

void CandleVideo::LoadSelectedVideo()
{
   if (mGeneratedVideoIndex < 0 || mGeneratedVideoIndex >= (int)mGeneratedVideoPaths.size())
   {
      SetStatusText("no generated video selected");
      return;
   }

   LoadVideoIntoTarget(mGeneratedVideoPaths[mGeneratedVideoIndex]);
}

void CandleVideo::DeleteSelectedGeneratedVideo()
{
   if (mGeneratedVideoIndex < 0 || mGeneratedVideoIndex >= (int)mGeneratedVideoPaths.size())
   {
      SetStatusText("no generated video selected");
      return;
   }

   const int deletedIndex = mGeneratedVideoIndex;
   juce::File videoFile(mGeneratedVideoPaths[mGeneratedVideoIndex]);
   juce::File metadataFile = videoFile.withFileExtension(".meta");
   UnloadTargetMediaIfNeeded({ mGeneratedVideoPaths[mGeneratedVideoIndex] });
   const bool deletedVideo = DeleteFileWithRetries(videoFile.getFullPathName().toStdString());
   metadataFile.deleteFile();

   RefreshGeneratedVideoList();
   RefreshPromptChoices();

   if (deletedVideo && !mGeneratedVideoPaths.empty())
   {
      mGeneratedVideoIndex = std::min(deletedIndex, (int)mGeneratedVideoPaths.size() - 1);
      LoadSelectedVideo();
      SetStatusText("deleted " + videoFile.getParentDirectory().getFileName().toStdString() +
                    ", loaded " + juce::File(mGeneratedVideoPaths[mGeneratedVideoIndex]).getParentDirectory().getFileName().toStdString());
   }
   else
   {
      SetStatusText(deletedVideo ? "deleted " + videoFile.getParentDirectory().getFileName().toStdString() : "couldn't delete selected video");
   }
}

void CandleVideo::DeleteAllGeneratedVideos()
{
   if (mGeneratedVideoPaths.empty())
   {
      SetStatusText("no generated videos to delete");
      return;
   }

   const bool confirm = juce::NativeMessageBox::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
                                                                "Delete generated CandleVideo videos?",
                                                                "Delete all generated videos for " + juce::String(Name()) + "? This cannot be undone.");
   if (!confirm)
      return;

   int deletedCount = 0;
   UnloadTargetMediaIfNeeded(mGeneratedVideoPaths);
   for (const std::string& path : mGeneratedVideoPaths)
   {
      juce::File videoFile(path);
      juce::File metadataFile = videoFile.withFileExtension(".meta");
      if (DeleteFileWithRetries(videoFile.getFullPathName().toStdString()))
         ++deletedCount;
      metadataFile.deleteFile();
   }

   mAutonextVideoLoadTime = -1;
   RefreshGeneratedVideoList();
   RefreshPromptChoices();
   SetStatusText("deleted " + ofToString(deletedCount) + " generated videos");
}

void CandleVideo::LoadVideoIntoTarget(const std::string& path)
{
   auto* acuneus = dynamic_cast<Acuneus*>(GetTarget());
   if (acuneus == nullptr)
   {
      AppendStatusText("\nconnect to an acuneus module");
      return;
   }

   if (!acuneus->IsInstanceOpen())
      acuneus->OpenInstance();
   acuneus->SetMediaPath(path);
   mLoadedVideoPath = juce::File(path).getFullPathName().toStdString();
   for (int i = 0; i < (int)mGeneratedVideoPaths.size(); ++i)
   {
      if (juce::File(mGeneratedVideoPaths[i]) == juce::File(mLoadedVideoPath))
      {
         mGeneratedVideoIndex = i;
         break;
      }
   }
   AppendStatusText("\nloaded " + juce::File(path).getFileName().toStdString());

   if (mAutonext)
   {
      const double durationSeconds = std::max(0.25, GetVideoDurationSeconds(path));
      mAutonextVideoLoadTime = gTime + durationSeconds * 1000.0;
      AppendStatusText(" (" + ofToString(durationSeconds, 2) + "s)");
   }
}

void CandleVideo::UnloadTargetMediaIfNeeded(const std::vector<std::string>& deletingPaths)
{
   if (mLoadedVideoPath.empty())
      return;

   const juce::File loadedFile(mLoadedVideoPath);
   const bool deletingLoadedVideo = std::any_of(deletingPaths.begin(), deletingPaths.end(),
                                                [&loadedFile](const std::string& path)
                                                {
                                                   return juce::File(path) == loadedFile;
                                                });
   if (!deletingLoadedVideo)
      return;

   auto* acuneus = dynamic_cast<Acuneus*>(GetTarget());
   if (acuneus != nullptr)
      acuneus->UnloadMedia();

   mLoadedVideoPath.clear();
   mAutonextVideoLoadTime = -1;
   AppendStatusText("\nunloaded current video for delete");
}

bool CandleVideo::DeleteFileWithRetries(const std::string& path) const
{
   const juce::File file(path);
   if (!file.existsAsFile())
      return true;

   for (int attempt = 0; attempt < 8; ++attempt)
   {
      if (file.deleteFile())
         return true;
      juce::Thread::sleep(75);
   }

   return !file.existsAsFile();
}

void CandleVideo::ButtonClicked(ClickButton* button, double time)
{
   if (button == mGenerateButton)
      StartGeneration();
   if (button == mLoadButton)
      LoadSelectedVideo();
   if (button == mDeleteVideoButton)
      DeleteSelectedGeneratedVideo();
   if (button == mDeleteAllVideosButton)
      DeleteAllGeneratedVideos();
   if (button == mMoreIdeasButton)
      GenerateMorePromptIdeas();
}

void CandleVideo::TextEntryComplete(TextEntry* entry)
{
}

void CandleVideo::FloatSliderUpdated(FloatSlider* slider, float oldVal, double time)
{
}

void CandleVideo::IntSliderUpdated(IntSlider* slider, int oldVal, double time)
{
   if (slider == mWidthSlider)
      mWidthParam = std::max(32, (mWidthParam / 32) * 32);
   if (slider == mHeightSlider)
      mHeightParam = std::max(32, (mHeightParam / 32) * 32);
   if (slider == mFramesSlider && mFrames > 1)
      mFrames = ((mFrames - 1) / 8) * 8 + 1;
}

void CandleVideo::DropdownUpdated(DropdownList* list, int oldVal, double time)
{
   if (list == mGeneratedVideoDropdown)
      LoadSelectedVideo();
   if (list == mPromptDropdown)
      ApplyPromptChoice();
}

void CandleVideo::CheckboxUpdated(Checkbox* checkbox, double time)
{
   if (checkbox == mAutoplayCheckbox)
   {
      if (mAutoplay)
         AutoplayNextPrompt();
      else
         mAutoplayNextGenerationTime = -1;
   }
   if (checkbox == mAutonextCheckbox)
   {
      if (mAutonext)
      {
         if (!mLoadedVideoPath.empty())
         {
            const double durationSeconds = std::max(0.25, GetVideoDurationSeconds(mLoadedVideoPath));
            mAutonextVideoLoadTime = gTime + durationSeconds * 1000.0;
            AppendStatusText("\nautonext: next in " + ofToString(durationSeconds, 2) + "s");
         }
         else if (!mGeneratedVideoPaths.empty())
         {
            if (mGeneratedVideoIndex < 0 || mGeneratedVideoIndex >= (int)mGeneratedVideoPaths.size())
               mGeneratedVideoIndex = 0;
            LoadSelectedVideo();
         }
         else
         {
            mAutonextVideoLoadTime = -1;
            AppendStatusText("\nautonext: no generated videos");
         }
      }
      else
      {
         mAutonextVideoLoadTime = -1;
         AppendStatusText("\nautonext off");
      }
   }
   if (checkbox == mUseMetadataVideoLabelsCheckbox)
      RefreshGeneratedVideoList();
}

void CandleVideo::DrawModule()
{
   mGeneratedVideoDropdown->Draw();
   mAutonextCheckbox->Draw();
   mLoadButton->Draw();
   mDeleteVideoButton->Draw();
   mDeleteAllVideosButton->Draw();
   mUseMetadataVideoLabelsCheckbox->Draw();
   mAutoloadCheckbox->Draw();
   mCpuCheckbox->Draw();
   mPromptEntry->Draw();
   mPromptDropdown->Draw();
   mMoreIdeasButton->Draw();
   mAutoplayCheckbox->Draw();
   if (mAutoplay)
   {
      std::string autoplayStatus = "next: queued";
      if (mGenerationInProgress)
         autoplayStatus = "next: generating";
      else if (mAutoplayNextGenerationTime > 0)
         autoplayStatus = "next: " + ofToString(std::max(0.0, (mAutoplayNextGenerationTime - gTime) / 1000.0), 1) + "s";

      const ofRectangle autoplayRect = mAutoplayCheckbox->GetRect(true);
      DrawTextNormal(autoplayStatus, autoplayRect.getMaxX() + 6, autoplayRect.y + 12, 9);
   }
   mGenerateButton->Draw();
   mWidthSlider->Draw();
   mHeightSlider->Draw();
   mFramesSlider->Draw();
   mFpsSlider->Draw();
   mStepsSlider->Draw();
   mSeedSlider->Draw();
   mRootEntry->Draw();
   mWeightsEntry->Draw();
   mFeaturesEntry->Draw();
   mOllamaModelEntry->Draw();
   mPromptCommandEntry->Draw();

   ClampStatusScroll();
   const ofRectangle statusRect = GetStatusRect();
   const std::vector<std::string> lines = GetWrappedStatusLines();
   constexpr int kStatusLineHeight = 11;

   ofPushStyle();
   ofFill();
   ofSetColor(255, 255, 255, 50);
   ofRect(statusRect.x, statusRect.y, statusRect.width, statusRect.height);
   ofSetColor(40, 40, 40);
   ofClipWindow(statusRect.x, statusRect.y, statusRect.width, statusRect.height, true);
   const int visibleLines = std::max(1, (int)statusRect.height / kStatusLineHeight);
   for (int i = 0; i < visibleLines && mStatusScrollLine + i < (int)lines.size(); ++i)
      DrawTextNormal(lines[mStatusScrollLine + i], statusRect.x + 5, statusRect.y + 12 + i * kStatusLineHeight, 9);
   ofResetClipWindow();
   ofPopStyle();
}

void CandleVideo::OnClicked(float x, float y, bool right)
{
   if (right && GetStatusRect().contains(x, y))
   {
      TheSynth->CopyTextToClipboard(GetStatusText());
      return;
   }

   IDrawableModule::OnClicked(x, y, right);
}

bool CandleVideo::MouseScrolled(float x, float y, float scrollX, float scrollY, bool isSmoothScroll, bool isInvertedScroll)
{
   if (!GetStatusRect().contains(x, y))
      return false;

   const int delta = (int)std::round(scrollX + scrollY);
   if (delta == 0)
      mStatusScrollLine += (scrollX + scrollY) > 0 ? -1 : 1;
   else
      mStatusScrollLine -= delta;
   ClampStatusScroll();
   return true;
}

ofRectangle CandleVideo::GetStatusRect() const
{
   return ofRectangle(5, 164, mWidth - 10, std::max(22.0f, mHeight - 169));
}

std::string CandleVideo::GetStatusText() const
{
   {
      std::lock_guard<std::mutex> statusLock(mStatusMutex);
      if (!mStatusString.empty())
         return mStatusString;
   }
   return "ready";
}

std::vector<std::string> CandleVideo::GetWrappedStatusLines() const
{
   const std::string text = GetStatusText();
   const float maxWidth = GetStatusRect().width - 10;
   constexpr float kTextSize = 9;
   std::vector<std::string> lines;
   std::stringstream paragraphs(text);
   std::string paragraph;

   while (std::getline(paragraphs, paragraph))
   {
      std::stringstream words(paragraph);
      std::string word;
      std::string line;
      while (words >> word)
      {
         std::string candidate = line.empty() ? word : line + " " + word;
         if (!line.empty() && GetStringWidth(candidate, kTextSize) > maxWidth)
         {
            lines.push_back(line);
            line = word;
         }
         else
         {
            line = candidate;
         }
      }
      lines.push_back(line);
   }

   if (lines.empty())
      lines.push_back("");
   return lines;
}

void CandleVideo::ClampStatusScroll()
{
   const int visibleLines = std::max(1, (int)GetStatusRect().height / 11);
   const int maxScroll = std::max(0, (int)GetWrappedStatusLines().size() - visibleLines);
   mStatusScrollLine = std::clamp(mStatusScrollLine, 0, maxScroll);
}

void CandleVideo::SetStatusText(const std::string& text)
{
   {
      std::lock_guard<std::mutex> statusLock(mStatusMutex);
      mStatusString = text;
   }
   ScrollStatusToEnd();
}

void CandleVideo::AppendStatusText(const std::string& text)
{
   if (text.empty())
      return;

   {
      std::lock_guard<std::mutex> statusLock(mStatusMutex);
      mStatusString += text;
      constexpr size_t kMaxStatusChars = 64000;
      if (mStatusString.size() > kMaxStatusChars)
         mStatusString.erase(0, mStatusString.size() - kMaxStatusChars);
   }
   ScrollStatusToEnd();
}

void CandleVideo::ScrollStatusToEnd()
{
   const int visibleLines = std::max(1, (int)GetStatusRect().height / 11);
   const int maxScroll = std::max(0, (int)GetWrappedStatusLines().size() - visibleLines);
   mStatusScrollLine = maxScroll;
}

void CandleVideo::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadString("target", moduleInfo);
   SetUpFromSaveData();
}

void CandleVideo::SaveLayout(ofxJSONElement& moduleInfo)
{
}

void CandleVideo::SetUpFromSaveData()
{
   SetTarget(TheSynth->FindModule(mModuleSaveData.GetString("target")));
}

void CandleVideo::SaveState(FileStreamOut& out)
{
   IDrawableModule::SaveState(out);
   out << std::string(mPromptEntry != nullptr ? mPromptEntry->GetText() : mPrompt);
   out << std::string(mRootEntry != nullptr ? mRootEntry->GetText() : mRoot);
   out << std::string(mWeightsEntry != nullptr ? mWeightsEntry->GetText() : mWeights);
   out << std::string(mFeaturesEntry != nullptr ? mFeaturesEntry->GetText() : mCargoFeatures);
   out << std::string(mOllamaModelEntry != nullptr ? mOllamaModelEntry->GetText() : mOllamaModel);
   out << std::string(mPromptCommandEntry != nullptr ? mPromptCommandEntry->GetText() : mPromptCommand);
   out << mWidthParam;
   out << mHeightParam;
   out << mFrames;
   out << mFps;
   out << mSteps;
   out << mSeed;
   out << mCpu;
   out << mAutoload;
   out << mAutoplay;
   out << mAutonext;
   out << mUseMetadataVideoLabels;
}

void CandleVideo::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);
   if (rev >= 1)
   {
      in >> mPrompt;
      in >> mRoot;
      in >> mWeights;
      in >> mCargoFeatures;
      if (rev >= 5)
         in >> mOllamaModel;
      if (rev >= 4)
         in >> mPromptCommand;
      in >> mWidthParam;
      in >> mHeightParam;
      in >> mFrames;
      if (rev >= 6)
         in >> mFps;
      in >> mSteps;
      in >> mSeed;
      in >> mCpu;
      in >> mAutoload;
   }
   if (rev >= 2)
      in >> mAutoplay;
   if (rev >= 3)
   {
      in >> mAutonext;
      in >> mUseMetadataVideoLabels;
   }

   SetPromptText(mPrompt);
   if (mRootEntry != nullptr)
      mRootEntry->SetText(mRoot);
   if (mWeightsEntry != nullptr)
      mWeightsEntry->SetText(mWeights);
   if (mFeaturesEntry != nullptr)
      mFeaturesEntry->SetText(mCargoFeatures);
   if (mOllamaModelEntry != nullptr)
      mOllamaModelEntry->SetText(mOllamaModel);
   if (mPromptCommandEntry != nullptr)
      mPromptCommandEntry->SetText(mPromptCommand);
   RefreshGeneratedVideoList();
   RefreshPromptChoices();
}

std::vector<IUIControl*> CandleVideo::ControlsToIgnoreInSaveState() const
{
   std::vector<IUIControl*> ignore;
   ignore.push_back(mGenerateButton);
   ignore.push_back(mMoreIdeasButton);
   ignore.push_back(mLoadButton);
   ignore.push_back(mDeleteVideoButton);
   ignore.push_back(mDeleteAllVideosButton);
   return ignore;
}
