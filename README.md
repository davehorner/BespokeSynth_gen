[![Build Status](https://dev.azure.com/awwbees/BespokeSynth/_apis/build/status/BespokeSynth.BespokeSynth?branchName=main)](https://dev.azure.com/awwbees/BespokeSynth/_build/latest?definitionId=1&branchName=main)
[![Contributor Covenant](https://img.shields.io/badge/Contributor%20Covenant-2.1-4baaaa.svg)](code_of_conduct.md)


# BespokeSynth_gen

This is the `_gen` fork of Bespoke Synth. It keeps the original modular synth workflow and adds local generative media patches that connect Bespoke audio, Acuneus GPU windows, StableAudio music generation, CandleVideo video generation, and Ollama prompt generation.

Upstream Bespoke Synth is a software modular synth that Ryan Challinor has been building since 2011.

[Nightly Build](https://github.com/BespokeSynth/BespokeSynth/releases/tag/Nightly) (updated every commit)

You can find the most recent builds for Mac/Windows/Linux at http://bespokesynth.com, or in the [Releases](https://github.com/BespokeSynth/BespokeSynth/releases) section on GitHub.

Join the [Bespoke Discord](https://discord.gg/YdTMkvvpZZ) for support and to discuss with the community.


## Documentation

* [Official documentation](https://www.bespokesynth.com/docs/)
* [Searchable, community-written documentation](https://github.com/BespokeSynth/BespokeSynthDocs/wiki)


## Screenshot

![screenshot](screenshot-1.png)


## Basic Overview/Tutorial Video

[![Bespoke Overview](https://img.youtube.com/vi/SYBc8X2IxqM/0.jpg)](https://www.youtube.com/watch?v=SYBc8X2IxqM)
* https://youtu.be/SYBc8X2IxqM

### Quick Reference

![quick reference](bespoke_quick_reference.png)


### Features

* live-patchable environment, so you can build while the music is playing
* VST, VST3, LV2 hosting
* Python livecoding
* MIDI & OSC controller mapping
* optional Acuneus integration for GPU shader windows, including one-click `acuneus/stableaudio`, `acuneus/candlevideo`, `acuneus/synth`, and `audio/video demo` welcome patches
* StableAudio node for local audio generation, looping, auto-generation, metadata browsing, Ollama music prompts, and Acuneus music automation
* CandleVideo node for local LTX video generation, Ollama video prompts, CRC32-based output filenames, autoload/autonext playback, and Acuneus media loading
* Works on Windows, Mac, and Linux

### _gen Fork Notes

The fork identifies itself as `BespokeSynth_gen` at the CMake project and JUCE product-name level. The internal CMake target is still named `BespokeSynth`, but the built app/executable is emitted as `BespokeSynth_gen` / `BespokeSynth_gen.exe`; the included `task run` and debug helpers use that forked executable name.

The main local integrations are:

* `R:\w\rust\c` for the Acuneus runtime and C ABI
* `R:\w\rust\candle-video` for the Candle LTX video generator
* local StableAudio model directories selected by CMake when available
* optional Ollama at `127.0.0.1:11434` for prompt ideas

### Acuneus / StableAudio Visualizer

When Bespoke is built with Acuneus support, the welcome screen includes an `acuneus/stableaudio` button. It creates:

```text
stableaudio -> acuneus/audio visualizer -> gain -> output
```

The shortcut enables StableAudio auto-generation, opens the Acuneus `audiovis` shader, passes audio through Acuneus to `gain`, and feeds the shader's spectrum buffer from the Bespoke audio cable. The Acuneus module can also automate shader sliders from incoming audio and control the Acuneus window position, size, resolution, time, FPS, overlay, and title bar.

The `acuneus/candlevideo` welcome shortcut creates:

```text
candlevideo -> acuneus/voronoi
```

The CandleVideo node runs the local `R:\w\rust\candle-video` LTX video generator, writes MP4 files under Bespoke's `candlevideo/<module-name>` data folder, and autoloads the generated `video.mp4` into the connected Acuneus module's media path. The node defaults to the local LTX 0.9.8 distilled weights in `R:\w\rust\candle-video\models\ltx-video` and uses the `flash-attn` Cargo feature by default; clear or change its Cargo features field if the local generator should run with different Candle features.

The `audio/video demo` welcome shortcut creates a combined generative patch:

```text
stableaudio -> acuneus/voronoi -> gain -> output
candlevideo -> acuneus/voronoi
```

StableAudio auto-generates looping audio and feeds Acuneus music automation. CandleVideo generates MP4s, autoloads them into the same Acuneus window, and can autonext through generated videos while new renders continue in the background. This is the preferred quick demo for using StableAudio and CandleVideo together.

The `acuneus/synth` welcome shortcut creates:

```text
keyboarddisplay -> acuneus/synth -> gain -> output
```

The keyboard sends notes to the Acuneus GPU synth. The synth sends PCM feedback back to Bespoke, and Bespoke plays it from the Acuneus audio output cable. Generated boolean params are shown as checkboxes; for the synth this includes `Local Audio`, which lets the synth process play through its own audio device in addition to Bespoke's routed output.

The Acuneus runtime and C ABI live in `R:\w\rust\c`. The Bespoke-side module is implemented in `Source/Acuneus.cpp` and `Source/Acuneus.h`.


### License

[GNU GPL v3](LICENSE)


### Releases

Sign up here to receive an email whenever I put out a new release: http://bespokesynth.substack.com/


### Contributing

[See our contributing guidelines](CONTRIBUTING.md)


### Building

Building Bespoke from source is easy and fun! The basic cmake prescription gives you a completed
executable which is ready to run on your system in many cases. If your system does not have `cmake` installed already you must do so.

```shell
git clone https://github.com/BespokeSynth/BespokeSynth   # replace this with your fork if you forked
cd BespokeSynth
git submodule update --init --recursive
cmake -Bignore/build -DCMAKE_BUILD_TYPE=Release
cmake --build ignore/build --parallel 4 --config Release
```

This will produce a release build in `ignore/build/Source/BespokeSynth_artefacts`.

There are a few useful options to the *first* cmake command which many folks choose to use.

* `-DBESPOKE_VST2_SDK_LOCATION=/path/to/sdk` will activate VST2 hosting support in your built
  copy of Bespoke if you have access to the VST SDK
* `-DBESPOKE_ASIO_SDK_LOCATION=/path/to/sdk` (windows only) will activate ASIO support on windows in your built copy of Bespoke if you have access to the ASIO SDK
* `-DBESPOKE_SPACEMOUSE_SDK_LOCATION=/path/to/sdk` (windows only) will activate SpaceMouse canvas navigation support on windows in your built copy of Bespoke if you have access to the SpaceMouse SDK
* `-DBESPOKE_PYTHON_ROOT=/...` will override the automatically detected python root. In some cases with M1 mac builds in homebrew this is useful.
* `-DCMAKE_BUILD_TYPE=Debug` will produce a build with debug information available
* `-A x64` (windows only) will force visual studio to build for 64 bit architectures, in the event this is not your default
* `-GXcode` (mac only) will eject xcode project files rather than the default make files
* `-DCMAKE_INSTALL_PREFIX=/usr` (only used on Linux) will set the `CMAKE_INSTALL_PREFIX` which guides both where your
  built bespoke looks for resources and also where it installs. After a build on Linux with this configured, you can
  do `sudo cmake --install ignore/build` and bespoke will install correctly into this directory. The cmake default is `/usr/local`.

The directory name `ignore/build` is arbitrary. Bespoke is set up to `.gitignore` everything in the `ignore` directory but you
can use any directory name you want for a build or have multiple builds also.

For building on Linux, you can also use [`just`](https://github.com/casey/just) to build by running `just build`. Use `just list` to see other options available with `just`.

To be able to build you will need a few things, depending on your OS

* All systems require an install of git
* On Windows:
    * Install Visual Studio 2019 Community Edition. When you install Visual Studio, make sure to include CLI tools and CMake, which are included in
      'Optional CLI support' and 'Toolset for Desktop' install bundles
    * Python from python.org
    * Run all commands from the visual studio command shell which will be available after you install VS.
* On MacOS: install xcode; install xcode command line tools with `xcode-select --install` and install cmake with `brew install cmake` if you use homebrew or from cmake.org if not
* On Linux you probably already have everything (gcc, git, etc...), but you will need to install required packages. The full list we
  install on a fresh ubuntu 20 box are listed in the azure-pipelines.yml
    * Some distributions may have slightly different package names like for instance Debian bookworm: You need to replace `alsa` and `alsa-tools` with `alsa-utils`

### Using Go Task for Build Automation

BespokeSynth has a [Go Task](https://taskfile.dev) Taskfile.yml as a cross-platform build automation tool. This makes it easy to configure, build, and run BespokeSynth with a single command.

```sh
task           # Configure and build BespokeSynth (default)
task run       # Run the built BespokeSynth executable
task clean     # Remove build artifacts
task install   # Install build prerequisites for your platform
```

Task will automatically detect your platform (Windows, macOS, Linux, or WSL) and use the appropriate build directory and commands.

For more details, see the `Taskfile.yml` in the repository root.

---

### One-Step Build Scripts: ensure_task

For convenience, you can use the provided scripts to automatically install Go Task (if needed) and build BespokeSynth in a single step:

- **Windows:** `ensure_task.bat`
- **Linux/macOS/WSL:** `ensure_task.sh`

#### Usage

**On Windows (cmd.exe):**

```cmd
ensure_task.bat [task arguments]
```

**On Linux/macOS/WSL (bash):**

```sh
./ensure_task.sh [task arguments]
```

- If Go Task is not found, the script will attempt to install it using your system's package manager or by downloading a release.
- Any arguments you provide will be passed directly to `task` (e.g., `run`, `clean`, etc.).
- By default, running the script with no arguments will perform the default build.

See the script files for more details or to customize the installation logic for your environment.
