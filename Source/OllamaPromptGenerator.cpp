/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#include "OllamaPromptGenerator.h"

#include "juce_core/juce_core.h"

#include <algorithm>
#include <memory>
#include <sstream>

namespace
{
struct CurlResult
{
   int exitCode{ -1 };
   std::string output;
   std::string error;
};

std::unique_ptr<juce::ChildProcess> sOllamaServerProcess;

std::string EscapeJsonString(const std::string& text)
{
   std::string escaped;
   escaped.reserve(text.size() + 16);

   for (const char c : text)
   {
      switch (c)
      {
         case '\\': escaped += "\\\\"; break;
         case '"': escaped += "\\\""; break;
         case '\b': escaped += "\\b"; break;
         case '\f': escaped += "\\f"; break;
         case '\n': escaped += "\\n"; break;
         case '\r': escaped += "\\r"; break;
         case '\t': escaped += "\\t"; break;
         default: escaped += c; break;
      }
   }

   return escaped;
}

juce::String GetOllamaExecutable()
{
   const juce::String localAppData = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", "");
   if (localAppData.isNotEmpty())
   {
      const juce::File installed = juce::File(localAppData).getChildFile("Programs/Ollama/ollama.exe");
      if (installed.existsAsFile())
         return installed.getFullPathName();
   }

   return "ollama";
}

CurlResult RunCurl(const juce::StringArray& args, int timeoutMs)
{
   CurlResult result;
   juce::ChildProcess process;
   if (!process.start(args))
   {
      result.error = "failed to start curl.exe for ollama";
      return result;
   }

   result.output = process.readAllProcessOutput().toStdString();
   const bool finished = process.waitForProcessToFinish(timeoutMs);
   if (!finished)
   {
      process.kill();
      result.error = "ollama api request timed out";
      return result;
   }

   result.exitCode = (int)process.getExitCode();
   return result;
}

bool IsOllamaServerReachable()
{
   juce::StringArray args;
   args.add("curl.exe");
   args.add("-sS");
   args.add("--max-time");
   args.add("2");
   args.add("http://127.0.0.1:11434/api/tags");

   return RunCurl(args, 3000).exitCode == 0;
}

bool StartOllamaServer(std::string& error)
{
   if (IsOllamaServerReachable())
      return true;

   if (sOllamaServerProcess != nullptr && sOllamaServerProcess->isRunning())
      return false;

   juce::StringArray args;
   args.add(GetOllamaExecutable());
   args.add("serve");

   sOllamaServerProcess = std::make_unique<juce::ChildProcess>();
   if (!sOllamaServerProcess->start(args, 0))
   {
      error = "failed to start ollama serve";
      sOllamaServerProcess.reset();
      return false;
   }

   for (int i = 0; i < 40; ++i)
   {
      juce::Thread::sleep(250);
      if (IsOllamaServerReachable())
         return true;
   }

   error = "started ollama serve, but the api did not become ready";
   return false;
}

CurlResult GenerateViaOllamaApi(const std::string& modelName, const std::string& instruction)
{
   const std::string request =
      "{\"model\":\"" + EscapeJsonString(modelName) +
      "\",\"prompt\":\"" + EscapeJsonString(instruction) +
      "\",\"stream\":false}";

   juce::StringArray args;
   args.add("curl.exe");
   args.add("-sS");
   args.add("--max-time");
   args.add("45");
   args.add("http://127.0.0.1:11434/api/generate");
   args.add("-H");
   args.add("Content-Type: application/json");
   args.add("-d");
   args.add(request);

   return RunCurl(args, 50000);
}
}

OllamaPromptGenerator::Result OllamaPromptGenerator::GenerateVideoPrompts(const std::string& model, const std::string& seedPrompt, int count)
{
   return GeneratePrompts(model, BuildInstruction(seedPrompt, count), count);
}

OllamaPromptGenerator::Result OllamaPromptGenerator::GenerateMusicPrompts(const std::string& model, const std::string& seedPrompt, const std::string& transportPrompt, int count)
{
   return GeneratePrompts(model, BuildMusicInstruction(seedPrompt, transportPrompt, count), count);
}

OllamaPromptGenerator::Result OllamaPromptGenerator::GeneratePrompts(const std::string& model, const std::string& instruction, int count)
{
   Result result;

   juce::String modelName(model);
   modelName = modelName.trim();
   if (modelName.isEmpty())
   {
      result.error = "set an ollama model first";
      return result;
   }

   CurlResult apiResult = GenerateViaOllamaApi(modelName.toStdString(), instruction);
   if (apiResult.exitCode == 7)
   {
      result.output = apiResult.output;
      result.output += "\nstarting ollama serve and retrying...\n";

      std::string startError;
      if (StartOllamaServer(startError))
      {
         apiResult = GenerateViaOllamaApi(modelName.toStdString(), instruction);
         result.output += apiResult.output;
      }
      else
      {
         result.error = startError.empty() ? "ollama api is not reachable" : startError;
         return result;
      }
   }

   if (!apiResult.error.empty())
   {
      result.error = apiResult.error;
      result.output += apiResult.output;
      return result;
   }

   result.output += apiResult.output;

   if (apiResult.exitCode != 0)
   {
      result.error = "ollama api request exited " + std::to_string(apiResult.exitCode);
      return result;
   }

   juce::var parsed = juce::JSON::parse(juce::String(apiResult.output));
   if (auto* object = parsed.getDynamicObject())
   {
      const juce::String error = object->getProperty("error").toString().trim();
      if (error.isNotEmpty())
      {
         result.error = error.toStdString();
         return result;
      }

      const juce::String response = object->getProperty("response").toString();
      result.prompts = ParsePrompts(response.toStdString(), count);
   }
   else
   {
      result.error = "ollama returned invalid json";
      return result;
   }

   if (result.prompts.empty())
   {
      result.error = "ollama returned no prompts";
      return result;
   }

   result.success = true;
   return result;
}

std::string OllamaPromptGenerator::BuildInstruction(const std::string& seedPrompt, int count)
{
   juce::String prompt(seedPrompt);
   prompt = prompt.trim();
   if (prompt.isEmpty())
      prompt = "surprising short cinematic video scenes";

   return "Create " + std::to_string(std::max(1, count)) +
          " diverse text-to-video prompts. Each prompt must describe visible video content only, not audio or music. "
          "Use concrete subjects, motion, camera, setting, and lighting. Do not mention seamless loops. "
          "Return only one prompt per line, no numbering, no markdown, no explanations. "
          "Use this as inspiration, but vary the ideas: " + prompt.toStdString();
}

std::string OllamaPromptGenerator::BuildMusicInstruction(const std::string& seedPrompt, const std::string& transportPrompt, int count)
{
   juce::String prompt(seedPrompt);
   prompt = prompt.trim();
   if (prompt.isEmpty())
      prompt = "distinctive electronic music cues for a modular synth patch";

   juce::String transport(transportPrompt);
   transport = transport.trim();
   if (transport.isEmpty())
      transport = "tempo 120 BPM, swing 50%, timesigtop 4, timesigbottom 4, swinginterval 8n";

   return "Create " + std::to_string(std::max(1, count)) +
          " unique Stable Audio text prompts for music generation. Each prompt must describe audible music only, not video or visuals. "
          "Use concrete genre, instrumentation, rhythm, texture, mood, production style, and intended use. "
          "Include duration in seconds and include or vary the transport details when musically useful. "
          "Avoid near-duplicates, generic stock phrases, artist names, copyrighted song names, and explanations. "
          "Return only one prompt per line, no numbering and no markdown. "
          "Transport context: " + transport.toStdString() + ". " +
          "Seed idea to riff on: " + prompt.toStdString();
}

std::vector<std::string> OllamaPromptGenerator::ParsePrompts(const std::string& output, int maxCount)
{
   std::vector<std::string> prompts;
   std::stringstream lines(output);
   std::string line;

   while (std::getline(lines, line))
   {
      juce::String prompt(line);
      prompt = prompt.trim().trimCharactersAtStart("-*0123456789.) \t").trim().unquoted();
      if (prompt.startsWithIgnoreCase("prompt:"))
         prompt = prompt.fromFirstOccurrenceOf(":", false, false).trim();
      if (prompt.isEmpty())
         continue;
      if (prompt.length() > 240)
         prompt = prompt.substring(0, 240);

      const std::string promptText = prompt.toStdString();
      if (std::find(prompts.begin(), prompts.end(), promptText) == prompts.end())
         prompts.push_back(promptText);
      if ((int)prompts.size() >= maxCount)
         break;
   }

   return prompts;
}
