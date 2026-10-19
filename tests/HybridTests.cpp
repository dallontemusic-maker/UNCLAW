// Проверки гибридного источника: звук во всех режимах, высота сэмпла, маршруты, старые пресеты.
// Сборка: cmake -B build -DUNCLAW_BUILD_TESTS=ON && cmake --build build --target UnclawHybridTests
// Запуск: UnclawHybridTests [папка для скриншотов интерфейса]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cmath>
#include <iostream>

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 512;
int failures = 0;

void check(bool ok, const juce::String& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (!ok) ++failures;
}

void setParam(WavetableSynthAudioProcessor& p, const char* id, float value) {
    auto* param = p.parameters.getParameter(id);
    jassert(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

float getParam(WavetableSynthAudioProcessor& p, const char* id) {
    return p.parameters.getRawParameterValue(id)->load();
}

struct Stats { float rms = 0, peak = 0, diffRatio = 0, freq = 0; bool finite = true; };

// Играет ноту и возвращает статистику левого канала за окно [skipSeconds, skipSeconds + seconds)
Stats play(WavetableSynthAudioProcessor& p, int note, double skipSeconds, double seconds, bool releaseFirst = true) {
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    if (releaseFirst) {   // гасим всё, что звучало, и даём хвостам затихнуть
        midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        for (int i = 0; i < (int) (2.0 * kRate) / kBlock; ++i) { p.processBlock(buffer, midi); midi.clear(); }
    }
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

    std::vector<float> capture;
    const int skip = (int) (skipSeconds * kRate), total = skip + (int) (seconds * kRate);
    for (int done = 0; done < total; done += kBlock) {
        p.processBlock(buffer, midi);
        midi.clear();
        for (int i = 0; i < kBlock; ++i)
            if (done + i >= skip && done + i < total) capture.push_back(buffer.getSample(0, i));
    }
    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
    p.processBlock(buffer, midi);

    Stats s;
    double sum = 0, diff = 0;
    int crossings = 0;
    for (size_t i = 0; i < capture.size(); ++i) {
        const float x = capture[i];
        s.finite = s.finite && std::isfinite(x);
        sum += (double) x * x;
        s.peak = std::max(s.peak, std::abs(x));
        if (i > 0) {
            diff += std::pow((double) x - capture[i - 1], 2.0);
            if (capture[i - 1] < 0.0f && x >= 0.0f) ++crossings;
        }
    }
    s.rms = (float) std::sqrt(sum / (double) std::max<size_t>(1, capture.size()));
    s.diffRatio = sum > 0 ? (float) (diff / sum) : 0.0f;   // доля высоких частот: белый > розовый > коричневый
    s.freq = (float) (crossings / seconds);
    return s;
}

juce::File writeSineWav(const juce::File& dir, double rate, double hz, double seconds) {
    auto file = dir.getChildFile("sine440.wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 1, 24, {}, 0));
    if (writer == nullptr) return {};
    stream.release();
    juce::AudioBuffer<float> b(1, (int) (rate * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        b.setSample(0, i, 0.7f * (float) std::sin(juce::MathConstants<double>::twoPi * hz * i / rate));
    writer->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    return file;
}

void isolateHybrid(WavetableSynthAudioProcessor& p) {
    for (auto id : { "OSC1_ON", "OSC2_ON", "REV_ON", "DEL_ON", "SAT_ON", "FILTER_ON", "ARP_ON" })
        setParam(p, id, 0.0f);
    setParam(p, "SUB_LEVEL", 0.8f);
    setParam(p, "SUB_ENV_ATTACK", 0.001f);
    setParam(p, "SUB_ENV_SUSTAIN", 1.0f);
    setParam(p, "SUB_ROUTE", 0.0f);
}

void snapshot(juce::Component& c, const juce::File& file) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(150);
    const auto image = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out(file);
    juce::PNGImageFormat().writeImageToStream(image, out);
    std::cout << "  shot  " << file.getFullPathName() << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI gui;
    WavetableSynthAudioProcessor p;
    p.setPlayConfigDetails(0, 2, kRate, kBlock);
    p.prepareToPlay(kRate, kBlock);
    isolateHybrid(p);

    std::cout << "SUB" << std::endl;
    setParam(p, "SUB_MODE", 0.0f);
    auto s = play(p, 48, 0.1, 1.0);   // C3 = 130.8 Hz, -1 oct = 65.4 Hz
    check(s.finite && s.rms > 0.1f, "sine sub is audible, rms " + juce::String(s.rms, 3));
    check(std::abs(s.freq - 65.4f) < 2.0f, "sine sub is one octave below: " + juce::String(s.freq, 1) + " Hz");
    setParam(p, "SUB_OCTAVE", 1.0f);
    s = play(p, 48, 0.1, 1.0);
    check(std::abs(s.freq - 32.7f) < 2.0f, "-2 oct: " + juce::String(s.freq, 1) + " Hz");
    setParam(p, "SUB_OCTAVE", 0.0f);
    setParam(p, "SUB_SHAPE", 1.0f);
    s = play(p, 48, 0.1, 1.0);
    check(s.rms > 0.1f && std::abs(s.freq - 65.4f) < 2.0f, "square sub, " + juce::String(s.freq, 1) + " Hz");
    setParam(p, "SUB_SHAPE", 0.0f);

    std::cout << "NOISE" << std::endl;
    setParam(p, "SUB_MODE", 1.0f);
    float ratios[3] {};
    for (int c = 0; c < 3; ++c) {
        setParam(p, "NOISE_COLOR", (float) c);
        s = play(p, 60, 0.1, 0.5);
        ratios[c] = s.diffRatio;
        check(s.finite && s.rms > 0.02f, juce::String("noise colour ") + juce::String(c) + " audible, rms " + juce::String(s.rms, 3));
    }
    check(ratios[0] > ratios[1] && ratios[1] > ratios[2],
          "spectrum gets darker white > pink > brown: " + juce::String(ratios[0], 3) + " / " + juce::String(ratios[1], 4)
              + " / " + juce::String(ratios[2], 5));
    setParam(p, "NOISE_COLOR", 1.0f);
    setParam(p, "NOISE_TILT", 12.0f);
    const float bright = play(p, 60, 0.1, 0.5).diffRatio;
    setParam(p, "NOISE_TILT", -12.0f);
    const float dark = play(p, 60, 0.1, 0.5).diffRatio;
    check(bright > dark * 2.0f, "Tilt changes brightness: " + juce::String(bright, 3) + " vs " + juce::String(dark, 4));
    setParam(p, "NOISE_TILT", 0.0f);

    std::cout << "SAMPLE" << std::endl;
    const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("unclaw-tests");
    tmp.createDirectory();
    const auto wavFile = writeSineWav(tmp, 44100.0, 440.0, 1.0);
    check(p.loadHybridSample(wavFile), "loads a 44.1 kHz WAV: " + p.getHybridStatus());
    check(!p.loadHybridSample(tmp.getChildFile("missing.wav")), "missing file is reported: " + p.getHybridStatus());
    setParam(p, "SUB_MODE", 2.0f);
    setParam(p, "SMP_ROOT", 69.0f);
    s = play(p, 69, 0.05, 0.5);
    check(s.finite && std::abs(s.freq - 440.0f) < 6.0f, "root note plays at original pitch (48 kHz host): " + juce::String(s.freq, 1) + " Hz");
    s = play(p, 81, 0.05, 0.3);
    check(std::abs(s.freq - 880.0f) < 10.0f, "keytrack: +12 notes = " + juce::String(s.freq, 1) + " Hz");
    setParam(p, "SMP_KEYTRACK", 0.0f);
    s = play(p, 81, 0.05, 0.3);
    check(std::abs(s.freq - 440.0f) < 6.0f, "keytrack off keeps root pitch: " + juce::String(s.freq, 1) + " Hz");
    setParam(p, "SMP_KEYTRACK", 1.0f);
    setParam(p, "SMP_PITCH", 12.0f);
    s = play(p, 69, 0.05, 0.3);
    check(std::abs(s.freq - 880.0f) < 10.0f, "Pitch +12 st = " + juce::String(s.freq, 1) + " Hz");
    setParam(p, "SMP_PITCH", 0.0f);
    s = play(p, 69, 1.2, 0.3);
    check(s.rms < 1.0e-4f, "one-shot ends after the file, rms " + juce::String(s.rms, 6));
    setParam(p, "SMP_LOOP", 1.0f);
    s = play(p, 69, 1.2, 0.3);
    check(s.rms > 0.05f, "loop keeps playing after the file end, rms " + juce::String(s.rms, 3));
    setParam(p, "SMP_REVERSE", 1.0f);
    setParam(p, "SMP_START", 0.5f);
    s = play(p, 69, 0.05, 1.5);
    check(s.finite && s.rms > 0.05f, "reverse + loop + start 50 % plays cleanly, rms " + juce::String(s.rms, 3));
    for (auto id : { "SMP_REVERSE", "SMP_LOOP", "SMP_START" }) setParam(p, id, 0.0f);

    std::cout << "ROUTING" << std::endl;
    setParam(p, "SUB_MODE", 1.0f);
    setParam(p, "NOISE_COLOR", 0.0f);
    setParam(p, "FILTER_ON", 1.0f);
    setParam(p, "CUTOFF", 40.0f);
    setParam(p, "FILTER_TYPE", 0.0f);
    const float filtered = play(p, 60, 0.1, 0.5).rms;
    setParam(p, "SUB_ROUTE", 1.0f);
    const float direct = play(p, 60, 0.1, 0.5).rms;
    check(direct > filtered * 5.0f, "FILTER route goes through the 40 Hz low-pass, DIRECT does not: "
          + juce::String(filtered, 4) + " vs " + juce::String(direct, 3));
    setParam(p, "SUB_MODE", 0.0f);
    setParam(p, "SAT_ON", 1.0f);
    setParam(p, "SAT_DRIVE", 10.0f);
    setParam(p, "SAT_MIX", 1.0f);
    setParam(p, "SUB_LEVEL", 1.0f);
    s = play(p, 48, 0.1, 0.5);
    const float crestDirect = s.peak / s.rms;
    setParam(p, "SUB_ROUTE", 2.0f);
    s = play(p, 48, 0.1, 0.5);
    const float crestDry = s.peak / s.rms;
    check(crestDirect < 1.25f && std::abs(crestDry - 1.414f) < 0.1f,
          "DIRECT is saturated (crest " + juce::String(crestDirect, 2) + "), DRY skips effects (crest "
              + juce::String(crestDry, 2) + ")");
    setParam(p, "SAT_ON", 0.0f);
    setParam(p, "SUB_ROUTE", 0.0f);

    std::cout << "LFO" << std::endl;
    setParam(p, "SUB_MODE", 2.0f);
    setParam(p, "LFO1_ON", 1.0f);
    setParam(p, "LFO1_DEST", 5.0f);   // Hybrid Pitch
    setParam(p, "LFO1_RATE", 5.0f);
    setParam(p, "LFO1_DEPTH", 1.0f);
    setParam(p, "LFO1_WAVE", 3.0f);   // square: ±12 st
    s = play(p, 69, 0.0, 0.2);
    check(s.finite && s.freq > 500.0f, "LFO -> Hybrid Pitch moves the sample: " + juce::String(s.freq, 1) + " Hz average");
    setParam(p, "LFO1_ON", 0.0f);

    std::cout << "STATE" << std::endl;
    juce::MemoryBlock state;
    p.getStateInformation(state);
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
    check(xml != nullptr && xml->getStringAttribute("hybridFile") == wavFile.getFullPathName(), "sample path is saved in the state");
    // пресет до гибридного источника: Noise, To Filter = off
    juce::XmlElement legacy("PARAMETERS");
    for (auto [id, value] : { std::pair<const char*, double> { "SUB_WAVE", 2.0 }, { "SUB_FILTER", 0.0 }, { "SUB_LEVEL", 0.5 } }) {
        auto* e = legacy.createNewChildElement("PARAM");
        e->setAttribute("id", id);
        e->setAttribute("value", value);
    }
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(legacy, legacyState);
    p.setStateInformation(legacyState.getData(), (int) legacyState.getSize());
    check(getParam(p, "SUB_MODE") == 1.0f && getParam(p, "SUB_ROUTE") == 1.0f && std::abs(getParam(p, "SUB_LEVEL") - 0.5f) < 1e-4f,
          "old preset: Noise -> NOISE mode, To Filter off -> DIRECT");

    if (argc > 1) {
        std::cout << "UI" << std::endl;
        const juce::File outDir(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
        outDir.createDirectory();
        p.loadHybridSample(wavFile);
        isolateHybrid(p);
        // для скриншотов: оба осциллятора и эффекты включены, аккорд зажат — видны осциллограф, индикатор и клавиши
        for (auto id : { "OSC1_ON", "OSC2_ON", "REV_ON", "FILTER_ON", "LFO1_ON" }) setParam(p, id, 1.0f);
        setParam(p, "CUTOFF", 2400.0f);
        setParam(p, "SUB_LEVEL", 0.45f);
        setParam(p, "LFO1_DEST", 1.0f);
        for (int note : { 60, 64, 67 }) p.keyboardState.noteOn(1, note, 0.9f);
        juce::AudioBuffer<float> audio(2, kBlock);
        juce::MidiBuffer noMidi;
        auto pump = [&] { for (int i = 0; i < 40; ++i) p.processBlock(audio, noMidi); };
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        editor->setSize(728, 750);
        const char* names[] { "sub", "noise", "sample" };
        for (int mode = 0; mode < 3; ++mode) {
            setParam(p, "SUB_MODE", (float) mode);
            setParam(p, "NOISE_COLOR", 1.0f);
            pump();
            snapshot(*editor, outDir.getChildFile(juce::String("hybrid-") + names[mode] + ".png"));
        }
        if (auto* synthEditor = dynamic_cast<WavetableSynthAudioProcessorEditor*>(editor.get())) {
            synthEditor->showPage(1);
            snapshot(*editor, outDir.getChildFile("page-effects.png"));
            synthEditor->showPage(0);
        }
        auto* target = dynamic_cast<juce::FileDragAndDropTarget*>(editor.get());
        const juce::StringArray files { wavFile.getFullPathName() };
        target->fileDragEnter(files, 600, 200);
        snapshot(*editor, outDir.getChildFile("hybrid-drag.png"));
        target->fileDragExit(files);
        editor.reset();
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String(failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
