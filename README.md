# UNCLAW Wavetable Synth

A JUCE-based wavetable synthesizer plugin. The project builds VST3 and Standalone on Windows, macOS, and Linux, plus AU on macOS.

## Build with GitHub Actions

Push this folder to a GitHub repository and the workflow in `.github/workflows/build.yml` will build the plugin for Windows, macOS, and Linux when you push to `main` or `master`, open a pull request against either branch, or start the workflow manually. Download the resulting plugin files from the run's **Artifacts** section.

## Build locally

Requirements:

- CMake 3.22 or newer
- A C++17 compiler supported by JUCE 8
- Git and network access for the first configure, because CMake downloads JUCE 8.0.4
- On Linux, JUCE's system dependencies: ALSA, FreeType, Fontconfig, X11, XRandR, Xinerama, XCursor, Xext, Xi, and OpenGL development packages

Configure and build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
```

Build products are written under `build/WavetableSynth_artefacts/Release/`.

## Project layout

- `Source/` — plugin processor, editor, wavetable loading, and oscillator code
- `.github/workflows/build.yml` — cross-platform GitHub Actions build
- `CMakeLists.txt` — JUCE dependency and plugin target configuration
