# UNCLAW Wavetable Synth

Cross-platform JUCE instrument plug-in with wavetable oscillators, a sub oscillator, envelopes, filters, two LFOs, effects, an arpeggiator, and an on-screen keyboard.

## Formats

- VST3 and Standalone on Windows, macOS, and Linux
- AU on macOS

The project downloads JUCE 8.0.4 with CMake FetchContent during configuration, so the first configure needs network access.

## Build locally

Requirements: CMake 3.22 or newer and a C++17 compiler. Linux also needs the JUCE system development packages used by the GitHub Actions workflow.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
```

On macOS, the plug-in formats are VST3, AU, and Standalone. On Windows and Linux, they are VST3 and Standalone.

## Build with GitHub Actions

Push the project to GitHub and run the **Build Wavetable Synthesizer** workflow. It builds Windows, macOS, and Linux artifacts. The workflow also supports manual runs from the Actions tab. Download the platform artifact from the completed workflow run.

Build outputs are placed under `build/WavetableSynth_artefacts/Release/`.
