#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include "WavetableOscillator.h"

class WavetableSynthAudioProcessor : public juce::AudioProcessor {
public:
    WavetableSynthAudioProcessor();
    ~WavetableSynthAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Обязательные виртуальные методы JUCE AudioProcessor
    const juce::String getName() const override { return "WavetableSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;
    // Загрузка вейвтейбла из аудиофайла для осциллятора 1 или 2.
    // loadCustomWavetable читает файл в вызывающем потоке; loadWavetableAsync делает это в фоне (для браузера).
    bool loadCustomWavetable(const juce::File& file, int oscIndex);
    void loadWavetableAsync(const juce::File& file, int oscIndex);
    juce::String getWavetableStatus(int oscIndex) const;   // текст для интерфейса: что загружено / почему не вышло
    juce::File getWavetableFile(int oscIndex) const;
    static constexpr int oscilloscopeSize = 512;
    void copyOscilloscopeBuffer(float* destination, int count) const;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateVoiceWavetables();

    // --- арпеджиатор ---
    void processArpeggiator(juce::MidiBuffer& midi, int numSamples);
    void resetArpeggiator();
    int nextArpNote(int mode, int octaves);
    double currentBpm() const;
    float param(const char* id) const { return parameters.getRawParameterValue(id)->load(); }

    std::vector<int> heldNotes, arpSorted, arpSeq;   // память резервируется заранее, без аллокаций в аудио-потоке
    juce::MidiBuffer arpOut;
    juce::Random arpRandom;
    double arpCountdown = 0.0;
    int arpGateCountdown = 0, arpStepIndex = 0, arpCurrentNote = -1;
    juce::uint8 arpVelocity = 100;
    bool arpWasOn = false;

    // Указатели на параметры осцилляторов (искать по строке в аудио-потоке не нужно)
    struct OscParamPtrs {
        std::atomic<float> *wave = nullptr, *wtPos = nullptr, *level = nullptr, *octave = nullptr, *semi = nullptr,
                           *fine = nullptr, *phase = nullptr, *pan = nullptr, *unison = nullptr, *detune = nullptr, *stereo = nullptr;
    };
    std::array<OscParamPtrs, 2> oscParams;

    juce::Synthesiser synth;
    juce::dsp::Reverb reverb;

    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    Delay delayLine;
    std::array<float, 2> delayLowpass {};
    juce::SmoothedValue<float> delayTimeSm;
    double currentSampleRate = 44100.0;

    // Вейвтейблы: GUI-поток кладёт новые таблицы под замком, аудио-поток забирает их try-lock'ом.
    std::shared_ptr<const WaveTable> wavetable1, wavetable2;
    std::vector<std::shared_ptr<const WaveTable>> retiredTables; // старые таблицы освобождаются в GUI-потоке
    juce::SpinLock wavetableLock;
    std::atomic<bool> wavetablesDirty { true };

    void setWavetableStatus(int idx, const juce::String& text, const juce::File& loaded);
    mutable juce::CriticalSection statusLock;
    std::array<juce::String, 2> statusText { juce::String("built-in wavetable"), juce::String("built-in wavetable") };
    std::array<juce::File, 2> loadedFile;
    std::array<std::atomic<float>, oscilloscopeSize> oscilloscopeBuffer {};

    juce::ThreadPool loadPool { 1 };   // объявлен последним: при удалении процессора останавливается первым

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableSynthAudioProcessor)
};
