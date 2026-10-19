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

## Hybrid source

The third source (formerly the sub oscillator) has three modes:

- **SUB** — sine or square, one or two octaves below the played note, with Fine and Pan.
- **NOISE** — white, pink (−3 dB/oct) or brown (−6 dB/oct) noise with Tilt (spectral slope) and Stereo (L/R decorrelation).
- **SAMPLE** — a sampler. Drop a `.wav`, `.aif`/`.aiff` or `.flac` file onto the card (from the waveform browser or from Finder/Explorer), click the sample display to browse, or pick **Smp** in the browser. Controls: Pitch (±24 st), Fine, Start, Loop (from Start to the end), Reverse, Keytrack and Root note. The sample path is saved with the project.

All modes share the amp envelope and a **ROUTE** switch:

- **FILTER** — through the filter, then the effects.
- **DIRECT** — past the filter, through the effects.
- **DRY** — past the filter and the effects, straight to the master.

LFO 1 and LFO 2 have three extra destinations: Hybrid Pitch, Hybrid Level and Noise Tilt. Presets saved before this change open with the same sound: the old Noise wave becomes NOISE mode and "To Filter" off becomes DIRECT.

## Tests

```sh
cmake -B build -DUNCLAW_BUILD_TESTS=ON
cmake --build build --target UnclawHybridTests
./build/UnclawHybridTests_artefacts/Release/UnclawHybridTests [folder-for-ui-screenshots]
```
