#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#include "WavetableOscillator.h"
#include "HybridSource.h"

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
    juce::MidiKeyboardState keyboardState;
    void setUiPitchWheelValue(int value) { uiPitchWheelValue.store(value); }
    void setUiModWheelValue(int value) { uiModWheelValue.store(value); }
    float getOutputPeak(int channel) const { return outputPeaks[(size_t) juce::jlimit(0, 1, channel)].load(); }
    // Загрузка вейвтейбла из аудиофайла для осциллятора 1 или 2.
    // loadCustomWavetable читает файл в вызывающем потоке; loadWavetableAsync делает это в фоне (для браузера).
    bool loadCustomWavetable(const juce::File& file, int oscIndex);
    void loadWavetableAsync(const juce::File& file, int oscIndex,
                            std::function<void(bool)> completion = {});
    juce::String getWavetableStatus(int oscIndex) const;   // текст для интерфейса: что загружено / почему не вышло
    juce::File getWavetableFile(int oscIndex) const;
    std::array<float, 64> getWavetablePreview(int oscIndex);

    // Гибридный источник: сэмпл для режима SAMPLE (тот же механизм подмены, что и у вейвтейблов)
    bool loadHybridSample(const juce::File& file);
    void loadHybridSampleAsync(const juce::File& file, std::function<void(bool)> completion = {});
    struct HybridSampleView { std::vector<float> peaks; juce::String name; double seconds = 0.0; };
    HybridSampleView getHybridSampleView();
    juce::String getHybridStatus() const;
    juce::File getHybridFile() const;
    int getHybridSampleVersion() const { return hybridSampleVersion.load(); }
    static bool isSupportedAudioFile(const juce::File& file);
    std::vector<float> oscilloscopeBuffer;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateVoiceWavetables();
    void applyVoiceMode (juce::MidiBuffer& midi, int mode, int voiceCount);

    struct MonoHeldNote { int note = -1, channel = 1; float velocity = 1.0f; };
    std::vector<MonoHeldNote> monoHeldNotes;
    std::vector<MonoHeldNote> polyHeldNotes;
    juce::MidiBuffer voiceModeMidi;
    int activeMonoNote = -1, activeMonoChannel = 1, previousVoiceMode = 0, previousVoiceCount = 8;

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
    std::atomic<int> uiPitchWheelValue { 8192 }, uiModWheelValue { 0 };
    std::array<std::atomic<float>, 2> outputPeaks { 0.0f, 0.0f };
    int lastUiPitchWheelValue = 8192, lastUiModWheelValue = 0;

    juce::Synthesiser synth;
    juce::dsp::Reverb reverb;

    using Delay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;
    Delay delayLine;
    std::array<float, 2> delayLowpass {};
    juce::SmoothedValue<float> delayTimeSm;
    bool delayWasEnabled = true;
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

    std::shared_ptr<const hybrid::Sample> hybridSample;
    std::vector<std::shared_ptr<const hybrid::Sample>> retiredSamples;
    juce::AudioBuffer<float> dryBus;   // маршрут DRY: голоса пишут сюда, шина подмешивается после эффектов
    juce::String hybridStatus { "drop an audio file here" };
    juce::File hybridFile;
    std::atomic<int> hybridSampleVersion { 0 };

    juce::ThreadPool loadPool { 1 };   // объявлен последним: при удалении процессора останавливается первым

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableSynthAudioProcessor)
};
