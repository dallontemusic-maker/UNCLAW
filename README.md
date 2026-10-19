# UNCLAW Wavetable Synth

A JUCE-based wavetable synthesizer plugin. It builds as VST3 and Standalone on Windows, macOS, and Linux, and as AU on macOS.

## GitHub Actions

Push this folder to a GitHub repository. The workflow in `.github/workflows/build.yml` builds the plugin when you push to `main` or `master`, open a pull request against either branch, or start the workflow manually. Download build results from the run's **Artifacts** section.

## Local build

Requirements: CMake 3.22 or newer, a C++17 compiler supported by JUCE 8, Git, and network access for the first configure (CMake downloads JUCE 8.0.4). On Linux, install JUCE's ALSA, FreeType, Fontconfig, X11, XRandR, Xinerama, XCursor, Xext, Xi, and OpenGL development packages.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
```

Build products are placed under `build/WavetableSynth_artefacts/Release/`.
