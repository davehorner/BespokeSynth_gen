/*
 ==============================================================================

 This file was auto-generated!

 It contains the basic startup code for a Juce application.

 ==============================================================================
 */

#include "juce_gui_basics/juce_gui_basics.h"
#include <memory>
#include "VSTScanner.h"
#include "SynthGlobals.h"
#include "ModularSynth.h"

#include "VersionInfo.h"

#if BESPOKE_WINDOWS
#include <dwmapi.h>
#include <windows.h>
#endif

using namespace juce;

Component* createMainContentComponent();
std::unique_ptr<juce::ApplicationProperties> appProperties;
void SetStartupSaveStateFile(const String& bskPath, Component* mainComponent);

namespace
{
String StripMatchingQuotes(String value)
{
   value = value.trim();
   if (value.length() >= 2 && ((value.startsWithChar('"') && value.endsWithChar('"')) || (value.startsWithChar('\'') && value.endsWithChar('\''))))
      return value.substring(1, value.length() - 1);
   return value;
}

bool HasSingleFlag(const StringArray& args)
{
   for (const auto& arg : args)
   {
      if (arg == "--single")
         return true;
   }
   return false;
}

String ExtractMpvMedia(const StringArray& args)
{
   for (int i = 0; i < args.size(); ++i)
   {
      const String arg = args[i];
      if (arg == "--mpv" || arg == "--mpv-url")
      {
         if (i + 1 < args.size())
            return StripMatchingQuotes(args[i + 1]);
      }
      else if (arg.startsWith("--mpv="))
      {
         return StripMatchingQuotes(arg.fromFirstOccurrenceOf("=", false, false));
      }
      else if (arg.startsWith("--mpv-url="))
      {
         return StripMatchingQuotes(arg.fromFirstOccurrenceOf("=", false, false));
      }
   }
   return {};
}

String ExtractMpvAutomationMedia(const StringArray& args)
{
   for (int i = 0; i < args.size(); ++i)
   {
      const String arg = args[i];
      if (arg == "--mpv-auto" || arg == "--mpv-automation")
      {
         if (i + 1 < args.size())
            return StripMatchingQuotes(args[i + 1]);
      }
      else if (arg.startsWith("--mpv-auto="))
      {
         return StripMatchingQuotes(arg.fromFirstOccurrenceOf("=", false, false));
      }
      else if (arg.startsWith("--mpv-automation="))
      {
         return StripMatchingQuotes(arg.fromFirstOccurrenceOf("=", false, false));
      }
   }
   return {};
}

String ExtractFlagValueFromRawCommandLine(const String& commandLine, const String& flag)
{
   const String equalsPrefix = flag + "=";
   const int equalsIndex = commandLine.indexOf(equalsPrefix);
   if (equalsIndex >= 0)
   {
      String value = commandLine.substring(equalsIndex + equalsPrefix.length()).trim();
      return StripMatchingQuotes(value);
   }

   const int flagIndex = commandLine.indexOf(flag);
   if (flagIndex < 0)
      return {};

   String rest = commandLine.substring(flagIndex + flag.length()).trimStart();
   if (rest.isEmpty())
      return {};

   if (rest.startsWithChar('"') || rest.startsWithChar('\''))
   {
      const juce_wchar quote = rest[0];
      const int endQuote = rest.indexOfChar(1, quote);
      if (endQuote > 0)
         return rest.substring(1, endQuote);
      return rest.substring(1);
   }

   const int space = rest.indexOfChar(' ');
   if (space >= 0)
      return rest.substring(0, space);
   return rest;
}
}

juce::ApplicationProperties& getAppProperties()
{
   return *appProperties;
}

//==============================================================================
class BespokeApplication : public JUCEApplication
{
public:
   //==============================================================================
   BespokeApplication() = default;

   const String getApplicationName() override { return Bespoke::APP_NAME; }
   const String getApplicationVersion() override { return Bespoke::VERSION; }
   bool moreThanOneInstanceAllowed() override { return !HasSingleFlag(JUCEApplication::getCommandLineParameterArray()); }

   //==============================================================================
   void initialise(const String& commandLine) override
   {
      // Parse command line arguments that should cause us to exit
      auto cliArgv = JUCEApplication::getCommandLineParameterArray();
      for (int i = 0; i < cliArgv.size(); ++i)
      {
         bool should_exit = false;
         juce::String argument = cliArgv[i];
         if (argument == "-h" || argument == "--help")
         {
            std::cout << "A modular DAW for Mac, Windows, and Linux.\n"
                      << "\n"
                      << "Usage: BespokeSynth [OPTIONS] [path].json [path].bsk(t)\n"
                      << "\n"
                      << "Arguments:\n"
                      << "  [path].bsk(t)   the project file to open (must end in .bsk or .bskt)\n"
                      << "  [path].json     path to userprefs.json (must end in .json)\n"
                      << "\n"
                      << "Options:\n"
                      << "  -o, --option <option> <value>   Temporarily override settings in preferences file\n"
                      << "  --mpv <url-or-path>             Open media in an mpvplayer module\n"
                      << "  --mpv-auto <url-or-path>        Open mpv_player_automation with this media URL\n"
                      << "  --single                        With --mpv or --mpv-auto, add to the existing Bespoke instance\n"
                      << "  -h, --help                      Print help\n"
                      << "  -v, --version                   Print version\n"
                      << std::flush;
            should_exit = true;
         }
         else if (argument == "-v" || argument == "--version")
         {
            std::cout << "bespoke synth " << GetBuildInfoString() << std::endl;
            should_exit = true;
         }
         else if (argument == "-o" || argument == "--option")
         {
            if ((cliArgv[i + 1].isEmpty()) || (cliArgv[i + 2].isEmpty()))
            {
               CliErrorExpectedOpt(argument);
               should_exit = true;
            }
         }

         if (should_exit == true)
         {
            JUCEApplicationBase::quit();
            return;
         }
      }

      auto scannerSubprocess = std::make_unique<PluginScannerSubprocess>();

      if (scannerSubprocess->initialiseFromCommandLine(commandLine, kScanProcessUID))
      {
         storedScannerSubprocess = std::move(scannerSubprocess);
         return;
      }

      const String initialMpvAutomationMedia = ExtractMpvAutomationMedia(cliArgv);
      const String initialRawMpvAutomationMedia = initialMpvAutomationMedia.isNotEmpty() ? initialMpvAutomationMedia : ExtractFlagValueFromRawCommandLine(commandLine, "--mpv-auto");
      if (initialRawMpvAutomationMedia.isNotEmpty())
         QueueStartupMpvAutomationMedia(initialRawMpvAutomationMedia.toStdString());

      mainWindow = std::make_unique<MainWindow>("bespoke synth");

      juce::PropertiesFile::Options options;
      options.applicationName = "Bespoke Synth";
      options.filenameSuffix = "settings";
      options.osxLibrarySubFolder = "Preferences";

      appProperties = std::make_unique<juce::ApplicationProperties>();
      appProperties->setStorageParameters(options);

#if BESPOKE_WINDOWS
      HWND hwnd = (HWND)this->mainWindow.get()->getWindowHandle();

      BOOL darkMode = Desktop::getInstance().isDarkModeActive();
      DwmSetWindowAttribute((HWND)hwnd, DWMWINDOWATTRIBUTE::DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
      UpdateWindow(hwnd);
#endif
   }

   // Prints an error for arguments that expected an argument but were not given one
   void CliErrorExpectedOpt(String argument)
   {
      std::cout << "Error: value is required for '" << argument << "' but none was supplied"
                << "\n\nFor more information, try '--help'"
                << std::endl;
   }

   void shutdown() override
   {
      // Add your application's shutdown code here..
      mainWindow.reset();
      appProperties.reset();
   }

   //==============================================================================
   void systemRequestedQuit() override
   {
      // This is called when the app is being asked to quit: you can ignore this
      // request and let the app carry on running, or call quit() to allow the app to close.
      quit();
   }

   void anotherInstanceStarted(const String& commandLine) override
   {
      // When another instance of the app is launched while this one is running,
      // this method is invoked, and the commandLine parameter tells you what
      // the other instance's command-line arguments were.

      // This is also called when opening the app with a file.
      if (commandLine.isNotEmpty() && commandLine.endsWith(".bsk"))
         SetStartupSaveStateFile(commandLine, mainWindow->getContentComponent());

      const StringArray args = StringArray::fromTokens(commandLine, true);
      const String mpvAutomationMedia = ExtractMpvAutomationMedia(args);
      const String rawMpvAutomationMedia = mpvAutomationMedia.isNotEmpty() ? mpvAutomationMedia : ExtractFlagValueFromRawCommandLine(commandLine, "--mpv-auto");
      if (rawMpvAutomationMedia.isNotEmpty() && TheSynth != nullptr)
         TheSynth->QueueMpvAutomationMedia(rawMpvAutomationMedia.toStdString());
      const String mpvMedia = ExtractMpvMedia(args);
      if (mpvMedia.isNotEmpty() && TheSynth != nullptr)
         TheSynth->QueueMpvMedia(mpvMedia.toStdString());
   }

   //==============================================================================
   /*
    This class implements the desktop window that contains an instance of
    our MainContentComponent class.
    */
   class MainWindow : public DocumentWindow
   {
   public:
      MainWindow(String name)
      : DocumentWindow(name,
                       Colours::lightgrey,
                       DocumentWindow::allButtons)
      {
         setUsingNativeTitleBar(true);
         setContentOwned(createMainContentComponent(), true);
         setResizable(true, true);

         centreWithSize(getWidth(), getHeight());
         setVisible(true);
      }

      void closeButtonPressed() override
      {
         // This is called when the user tries to close this window. Here, we'll just
         // ask the app to quit when this happens, but you can change this to do
         // whatever you need.
         JUCEApplication::getInstance()->systemRequestedQuit();
      }

      /* Note: Be careful if you override any DocumentWindow methods - the base
       class uses a lot of them, so by overriding you might break its functionality.
       It's best to do all your work in your content component instead, but if
       you really have to override any DocumentWindow methods, make sure your
       subclass also calls the superclass's method.
       */

   private:
      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
   };

private:
   std::unique_ptr<MainWindow> mainWindow;
   std::unique_ptr<PluginScannerSubprocess> storedScannerSubprocess;
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(BespokeApplication)
