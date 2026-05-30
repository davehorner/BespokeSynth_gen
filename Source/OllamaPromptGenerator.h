/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2026 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
**/

#pragma once

#include <string>
#include <vector>

class OllamaPromptGenerator
{
public:
   struct Result
   {
      bool success{ false };
      std::vector<std::string> prompts;
      std::string error;
      std::string output;
   };

   static Result GenerateVideoPrompts(const std::string& model, const std::string& seedPrompt, int count);
   static Result GenerateMusicPrompts(const std::string& model, const std::string& seedPrompt, const std::string& transportPrompt, int count);

private:
   static Result GeneratePrompts(const std::string& model, const std::string& instruction, int count);
   static std::string BuildInstruction(const std::string& seedPrompt, int count);
   static std::string BuildMusicInstruction(const std::string& seedPrompt, const std::string& transportPrompt, int count);
   static std::vector<std::string> ParsePrompts(const std::string& output, int maxCount);
};
