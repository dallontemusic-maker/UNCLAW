#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WavetableLoader.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int kMaxUnison = 7;

struct OscSettings {
    int wave = 0, octave = 0, semi = 0, unison = 1;
    float wtPos = 0.0f, level = 1.0f, pan = 0.0f, stereo = 0.7f;
    float detuneCents = 25.0f, fineCents = 0.0f, phaseDeg = 0.0f;
    bool filterOn = true, analogMode = false;
};

struct LfoSettings {
    int destination = 0, waveform = 0, syncDivision = 2;
    float rateHz = 2.0f, depth = 0.2f;
    bool enabled = false, tempoSync = false;
};

struct VoiceParams {
    std::array<OscSettings, 2> osc;
    int subWave = 2;
    float subLevel = 0.0f;
    bool subFilterOn = true;
    float cutoff = 1000.0f, resonance = 1.0f;
    bool filterEnabled = true;
    int filterType = 0;
    float pitchBendRange = 2.0f;
    std::array<juce::ADSR::Parameters, 2> ampEnvelopes;
    juce::ADSR::Parameters subAmpEnvelope;
    std::array<LfoSettings, 2> lfos;
    float bpm = 120.0f;
    juce::ADSR::Parameters filterAdsr;
    float filterEnvAmountSemitones = 0.0f;
};

WavetableOscillator::Source toSource(int idx) {
    return static_cast<WavetableOscillator::Source>(juce::jlimit(0, 5, idx));
}

// Sub: 0 = Sine, 1 = Square, 2 = Noise
WavetableOscillator::Source toSubSource(int idx) {
    switch (idx) {
        case 0:  return WavetableOscillator::Source::Sine;
        case 1:  return WavetableOscillator::Source::Square;
        default: return WavetableOscillator::Source::Noise;
    }
}

// Таблица по умолчанию (8 кадров: синус -> пила), чтобы синтезатор звучал и без загруженного WAV.
std::shared_ptr<const WaveTable> makeDefaultWavetable() {
    constexpr int numFrames = 8;
    constexpr int tableSize = 2048;
    auto table = std::make_shared<WaveTable>((size_t) numFrames, std::vector<float>((size_t) tableSize, 0.0f));
    for (int f = 0; f < numFrames; ++f) {
        const int harmonics = 1 + (f * 31) / (numFrames - 1);
        auto& frame = (*table)[(size_t) f];
        float peak = 0.0f;
        for (int i = 0; i < tableSize; ++i) {
            const double ph = (double) i / (double) tableSize;
            double s = 0.0;
            for (int h = 1; h <= harmonics; ++h)
                s += std::sin(juce::MathConstants<double>::twoPi * h * ph) / h;
            frame[(size_t) i] = (float) s;
            peak = std::max(peak, std::abs((float) s));
        }
        if (peak > 0.0f)
            for (auto& v : frame) v /= peak;
    }
    return table;
}

} // namespace

class SynthSound : public juce::SynthesiserSound {
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SynthVoice : public juce::SynthesiserVoice {
public:
    bool canPlaySound (juce::SynthesiserSound* sound) override {
        return dynamic_cast<SynthSound*>(sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override {
        baseFreq = (float) juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
        velocityGain = velocity;
        for (size_t oscIndex = 0; oscIndex < units.size(); ++oscIndex)
            analogDriftCents[oscIndex] = units[oscIndex].s.analogMode
                ? (random.nextFloat() * 2.0f - 1.0f) * 1.2f
                : 0.0f;
        updateFrequencies();

        for (auto& u : units) {
            for (int k = 0; k < u.n; ++k) {
                float ph = u.s.phaseDeg / 360.0f;
                if (k > 0) ph += random.nextFloat();   // остальные голоса юнисона стартуют со случайной фазы
                u.voices[(size_t) k].resetPhase(ph);
            }
            u.levelCur = u.s.level;   // сглаживание начинается с текущих значений, без "наезда"
            u.panCur = u.s.pan;
            u.stereoCur = u.s.stereo;
        }
        sub.setFrequency(baseFreq * 0.5f);   // Sub звучит на октаву ниже
        sub.resetPhase();
        for (auto& f : filters) f.reset();
        for (auto& env : ampEnvelopes) env.noteOn();
        subAmpEnvelope.noteOn();
        filterAdsr.noteOn();
        lfoPhase.fill(0.0f);
    }

    void stopNote (float, bool allowTailOff) override {
        for (auto& env : ampEnvelopes) env.noteOff();
        subAmpEnvelope.noteOff();
        filterAdsr.noteOff();
        if (!allowTailOff || (!ampEnvelopes[0].isActive() && !ampEnvelopes[1].isActive()
                              && !subAmpEnvelope.isActive())) clearCurrentNote();
    }

    void pitchWheelMoved (int value) override {
        pitchWheel = juce::jlimit(0, 16383, value);
        updateFrequencies();
    }
    void controllerMoved (int controller, int value) override {
        if (controller == 1) modWheel = juce::jlimit(0, 127, value) / 127.0f;
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock, int outputChannels) {
        currentSampleRate = sampleRate;
        smoothCoef = 1.0f - std::exp(-1.0f / (0.005f * (float) sampleRate));   // сглаживание 5 мс
        for (auto& u : units)
            for (auto& v : u.voices) v.prepare(sampleRate);
        sub.prepare(sampleRate);
        for (auto& env : ampEnvelopes) env.setSampleRate(sampleRate);
        subAmpEnvelope.setSampleRate(sampleRate);
        filterAdsr.setSampleRate(sampleRate);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
        spec.numChannels = (juce::uint32) juce::jmax(2, outputChannels);
        for (auto& f : filters) {
            f.prepare(spec);
            f.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        }
    }

    void setWavetables(const std::shared_ptr<const WaveTable>& wt1, const std::shared_ptr<const WaveTable>& wt2) {
        for (auto& v : units[0].voices) v.loadTable(wt1);
        for (auto& v : units[1].voices) v.loadTable(wt2);
    }

    void setParams(const VoiceParams& p) {
        for (size_t i = 0; i < 2; ++i) {
            auto& u = units[i];
            u.s = p.osc[i];
            u.n = juce::jlimit(1, kMaxUnison, u.s.unison);
            u.norm = 1.0f / std::sqrt((float) u.n);   // громкость почти не зависит от числа голосов
            for (int k = 0; k < kMaxUnison; ++k)
                u.spread[(size_t) k] = (u.n > 1) ? (2.0f * (float) k / (float) (u.n - 1) - 1.0f) : 0.0f;
            for (auto& v : u.voices) {
                v.setSource(toSource(u.s.wave));
                v.setWavetablePosition(u.s.wtPos);
            }
            ampEnvelopes[i].setParameters(p.ampEnvelopes[i]);
            filters[i].setCutoffFrequency(juce::jlimit(20.0f, (float)(0.45 * currentSampleRate), p.cutoff));
            filters[i].setResonance(p.resonance);
            filters[i].setType(p.filterType == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                                : (p.filterType == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                                     : juce::dsp::StateVariableTPTFilterType::lowpass));
        }
        filters[2].setCutoffFrequency(juce::jlimit(20.0f, (float)(0.45 * currentSampleRate), p.cutoff));
        filters[2].setResonance(p.resonance);
        filters[2].setType(p.filterType == 1 ? juce::dsp::StateVariableTPTFilterType::bandpass
                              : (p.filterType == 2 ? juce::dsp::StateVariableTPTFilterType::highpass
                                                   : juce::dsp::StateVariableTPTFilterType::lowpass));
        updateFrequencies();

        sub.setSource(toSubSource(p.subWave));
        subLevel = p.subLevel;
        subFilterOn = p.subFilterOn;
        subAmpEnvelope.setParameters(p.subAmpEnvelope);
        for (size_t i = 0; i < lfoSettings.size(); ++i) {
            lfoSettings[i] = p.lfos[i];
            if (lfoSettings[i].tempoSync) {
                static constexpr float beatRates[] { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
                const int division = juce::jlimit(0, 4, lfoSettings[i].syncDivision);
                lfoSettings[i].rateHz = juce::jmax(0.001f, p.bpm / 60.0f * beatRates[division]);
            }
        }
        filterBaseCutoff = juce::jlimit(20.0f, (float)(0.45 * currentSampleRate), p.cutoff);
        filterEnabled = p.filterEnabled;
        pitchBendRange = p.pitchBendRange;
        filterAdsr.setParameters(p.filterAdsr);
        filterEnvAmountSemitones = p.filterEnvAmountSemitones;
    }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override {
        if (!isVoiceActive()) return;
        const int numCh = outputBuffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i) {
            float left = 0.0f, right = 0.0f;
            const float filterEnv = filterAdsr.getNextSample();
            float filterLfoSemitones = 0.0f, pitchLfoSemitones = 0.0f;
            float wtLfoOffset[2] {};
            bool pitchLfoActive = false;
            for (size_t lfoIndex = 0; lfoIndex < lfoSettings.size(); ++lfoIndex) {
                const auto& lfo = lfoSettings[lfoIndex];
                if (!lfo.enabled || lfo.destination == 0) continue;
                const float value = lfoValue(lfo.waveform, lfoPhase[lfoIndex]) * lfo.depth;
                switch (lfo.destination) {
                    case 1: filterLfoSemitones += value * 24.0f; break;
                    case 2: wtLfoOffset[0] += value; break;
                    case 3: wtLfoOffset[1] += value; break;
                    case 4: pitchLfoSemitones += value * 12.0f; pitchLfoActive = true; break;
                }
                lfoPhase[lfoIndex] += lfo.rateHz / (float) currentSampleRate;
                if (lfoPhase[lfoIndex] >= 1.0f) lfoPhase[lfoIndex] -= std::floor(lfoPhase[lfoIndex]);
            }
            if (pitchLfoActive || lastPitchLfoSemitones != 0.0f) {
                updateFrequencies(pitchLfoSemitones);
                lastPitchLfoSemitones = pitchLfoSemitones;
            }
            if ((i & 7) == 0) {
                const float cutoff = juce::jlimit(20.0f, (float)(0.45 * currentSampleRate),
                    filterBaseCutoff * std::exp2((filterEnvAmountSemitones * filterEnv + modWheel * 24.0f
                                                  + filterLfoSemitones) / 12.0f));
                for (auto& filter : filters)
                    filter.setCutoffFrequency(cutoff);
            }

            for (size_t oscIndex = 0; oscIndex < units.size(); ++oscIndex) {
                auto& u = units[oscIndex];
                float oscLeft = 0.0f, oscRight = 0.0f;
                const float ampEnv = ampEnvelopes[oscIndex].getNextSample();
                u.levelCur += (u.s.level - u.levelCur) * smoothCoef;
                u.panCur += (u.s.pan - u.panCur) * smoothCoef;
                u.stereoCur += (u.s.stereo - u.stereoCur) * smoothCoef;
                const float gain = u.levelCur * u.norm * ampEnv;

                for (int k = 0; k < u.n; ++k) {
                    const float wtPosition = juce::jlimit(0.0f, 1.0f,
                        u.s.wtPos + wtLfoOffset[oscIndex]);
                    u.voices[(size_t) k].setWavetablePosition(wtPosition);
                    const float x = u.voices[(size_t) k].processSample() * gain;
                    // позиция голоса: Pan осциллятора + разброс юнисона * Stereo
                    const float pos = juce::jlimit(-1.0f, 1.0f, u.panCur + u.spread[(size_t) k] * u.stereoCur);
                    oscLeft += x * (pos > 0.0f ? 1.0f - pos : 1.0f);
                    oscRight += x * (pos < 0.0f ? 1.0f + pos : 1.0f);
                }
                oscLeft *= 0.5f;
                oscRight *= 0.5f;
                if (filterEnabled && u.s.filterOn) {
                    left += filters[oscIndex].processSample(0, oscLeft);
                    right += filters[oscIndex].processSample(1, oscRight);
                } else {
                    left += oscLeft;
                    right += oscRight;
                }
            }

            const float subSample = subLevel * subAmpEnvelope.getNextSample() * sub.processSample();
            if (filterEnabled && subFilterOn) {
                left += filters[2].processSample(0, subSample);
                right += filters[2].processSample(1, subSample);
            } else {
                left += subSample;
                right += subSample;
            }

            const float gainOut = velocityGain * 0.2f;
            const float outL = left * gainOut;
            const float outR = right * gainOut;

            if (numCh == 1) {
                outputBuffer.addSample(0, startSample + i, 0.5f * (outL + outR));
            } else {
                outputBuffer.addSample(0, startSample + i, outL);
                outputBuffer.addSample(1, startSample + i, outR);
                for (int ch = 2; ch < numCh; ++ch)
                    outputBuffer.addSample(ch, startSample + i, 0.5f * (outL + outR));
            }

            if (!ampEnvelopes[0].isActive() && !ampEnvelopes[1].isActive()
                && !subAmpEnvelope.isActive()) {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    static float lfoValue(int waveform, float phase) {
        switch (waveform) {
            case 1: return 1.0f - 4.0f * std::abs(phase - 0.5f); // triangle
            case 2: return 2.0f * phase - 1.0f;                  // saw
            case 3: return phase < 0.5f ? 1.0f : -1.0f;          // square
            default: return std::sin(juce::MathConstants<float>::twoPi * phase);
        }
    }

    struct OscUnit {
        std::array<WavetableOscillator, kMaxUnison> voices;
        std::array<float, kMaxUnison> spread {};
        OscSettings s;
        int n = 1;
        float norm = 1.0f;
        float levelCur = 1.0f, panCur = 0.0f, stereoCur = 0.7f;
    };

    // Высота каждого голоса: нота + Octave + Interval + Fine + разброс Detune (в центах)
    void updateFrequencies(float pitchLfoSemitones = 0.0f) {
        for (size_t oscIndex = 0; oscIndex < units.size(); ++oscIndex) {
            auto& u = units[oscIndex];
            const float baseCents = (float) (u.s.octave * 1200 + u.s.semi * 100)
                                  + u.s.fineCents + analogDriftCents[oscIndex];
            for (int k = 0; k < u.n; ++k) {
                const float bend = ((float) pitchWheel - 8192.0f) / 8192.0f * pitchBendRange * 100.0f;
                const float cents = baseCents + bend + pitchLfoSemitones * 100.0f
                                  + u.spread[(size_t) k] * u.s.detuneCents;
                u.voices[(size_t) k].setFrequency(baseFreq * std::exp2(cents / 1200.0f));
            }
        }
    }

    std::array<OscUnit, 2> units;
    std::array<float, 2> analogDriftCents {};
    std::array<LfoSettings, 2> lfoSettings;
    std::array<float, 2> lfoPhase {};
    float lastPitchLfoSemitones = 0.0f;
    WavetableOscillator sub;
    float subLevel = 0.0f;
    bool subFilterOn = true;
    std::array<juce::ADSR, 2> ampEnvelopes;
    juce::ADSR subAmpEnvelope;
    juce::ADSR filterAdsr;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 3> filters;
    float filterBaseCutoff = 1000.0f;
    bool filterEnabled = true;
    float filterEnvAmountSemitones = 0.0f;
    juce::Random random;
    double currentSampleRate = 44100.0;
    float smoothCoef = 0.01f;
    float baseFreq = 440.0f;
    float velocityGain = 1.0f;
    float pitchBendRange = 2.0f, modWheel = 0.0f;
    int pitchWheel = 8192;
};

WavetableSynthAudioProcessor::WavetableSynthAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout()) {

    for (int n = 0; n < 2; ++n) {
        const juce::String pre = "OSC" + juce::String(n + 1) + "_";
        auto& o = oscParams[(size_t) n];
        o.wave = parameters.getRawParameterValue(pre + "WAVE");
        o.wtPos = parameters.getRawParameterValue("WT_POS_" + juce::String(n + 1));
        o.level = parameters.getRawParameterValue(pre + "LEVEL");
        o.octave = parameters.getRawParameterValue(pre + "OCT");
        o.semi = parameters.getRawParameterValue(pre + "SEMI");
        o.fine = parameters.getRawParameterValue(pre + "FINE");
        o.phase = parameters.getRawParameterValue(pre + "PHASE");
        o.pan = parameters.getRawParameterValue(pre + "PAN");
        o.unison = parameters.getRawParameterValue(pre + "UNISON");
        o.detune = parameters.getRawParameterValue(pre + "DETUNE");
        o.stereo = parameters.getRawParameterValue(pre + "STEREO");
    }
    oscilloscopeBuffer.resize(512, 0.0f);
    heldNotes.reserve(128);
    polyHeldNotes.reserve(128);
    arpSorted.reserve(128);
    arpSeq.reserve(1100);
    wavetable1 = wavetable2 = makeDefaultWavetable();
    for (int i = 0; i < 8; ++i) synth.addVoice(new SynthVoice());
    synth.addSound(new SynthSound());
}

WavetableSynthAudioProcessor::~WavetableSynthAudioProcessor() {
    loadPool.removeAllJobs(true, 2000);
}

juce::AudioProcessorValueTreeState::ParameterLayout WavetableSynthAudioProcessor::createParameterLayout() {
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    using FloatAttr = juce::AudioParameterFloatAttributes;
    using IntAttr = juce::AudioParameterIntAttributes;
    const juce::StringArray oscWaves { "Wavetable", "Sine", "Triangle", "Saw", "Square", "Noise" };

    auto signedInt = [] (int v, int) { return juce::String(v > 0 ? "+" : "") + juce::String(v); };
    auto panText = [] (float v, int) {
        if (std::abs(v) < 0.005f) return juce::String("C");
        return juce::String(v < 0.0f ? "L" : "R") + juce::String((int) std::lround(std::abs(v) * 100.0f));
    };
    auto panValue = [] (const juce::String& t) {
        const auto s = t.trim().toUpperCase();
        const float n = (float) s.retainCharacters("0123456789.").getDoubleValue() / 100.0f;
        return s.startsWithChar('L') ? -n : (s.startsWithChar('R') ? n : 0.0f);
    };
    auto cents = [] (float v, int) { return juce::String((int) std::lround(v)) + " ct"; };
    auto degrees = [] (float v, int) { return juce::String((int) std::lround(v)) + juce::String(juce::CharPointer_UTF8("\xc2\xb0")); };

    for (int n = 1; n <= 2; ++n) {
        const juce::String pre = "OSC" + juce::String(n) + "_";
        const juce::String nm = "Osc " + juce::String(n) + " ";
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pre + "WAVE", nm + "Wave", oscWaves, 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>("WT_POS_" + juce::String(n), nm + "Position", 0.0f, 1.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "LEVEL", nm + "Level", 0.0f, 1.0f, 1.0f));
        params.push_back(std::make_unique<juce::AudioParameterInt>(pre + "OCT", nm + "Octave", -3, 3, 0, IntAttr().withStringFromValueFunction(signedInt)));
        params.push_back(std::make_unique<juce::AudioParameterInt>(pre + "SEMI", nm + "Interval", -12, 12, 0, IntAttr().withStringFromValueFunction(signedInt)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "FINE", nm + "Fine", juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f, FloatAttr().withStringFromValueFunction(cents)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "PHASE", nm + "Phase", juce::NormalisableRange<float>(0.0f, 360.0f, 1.0f), 0.0f, FloatAttr().withStringFromValueFunction(degrees)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "PAN", nm + "Pan", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f, FloatAttr().withStringFromValueFunction(panText).withValueFromStringFunction(panValue)));
        params.push_back(std::make_unique<juce::AudioParameterInt>(pre + "UNISON", nm + "Unison", 1, kMaxUnison, 1));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "DETUNE", nm + "Detune", juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f, FloatAttr().withStringFromValueFunction(cents)));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "STEREO", nm + "Stereo", 0.0f, 1.0f, 0.7f));
        params.push_back(std::make_unique<juce::AudioParameterBool>(pre + "FILTER", nm + "To Filter", true));
        params.push_back(std::make_unique<juce::AudioParameterBool>(pre + "ANALOG", nm + "Analog Mode", false));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "ENV_ATTACK", nm + "Amp Attack", 0.001f, 5.0f, 0.01f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "ENV_DECAY", nm + "Amp Decay", 0.01f, 5.0f, 0.2f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "ENV_SUSTAIN", nm + "Amp Sustain", 0.0f, 1.0f, 0.8f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "ENV_RELEASE", nm + "Amp Release", 0.01f, 5.0f, 0.3f));
    }
    params.push_back(std::make_unique<juce::AudioParameterChoice>("SUB_WAVE", "Sub Wave", juce::StringArray{"Sine", "Square", "Noise"}, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_LEVEL", "Sub Level", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("SUB_FILTER", "Sub To Filter", true));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_ENV_ATTACK", "Sub Amp Attack", 0.001f, 5.0f, 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_ENV_DECAY", "Sub Amp Decay", 0.01f, 5.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_ENV_SUSTAIN", "Sub Amp Sustain", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_ENV_RELEASE", "Sub Amp Release", 0.01f, 5.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CUTOFF", "Filter Cutoff", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RESONANCE", "Filter Resonance", 0.1f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("FILTER_ON", "Filter On", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("FILTER_TYPE", "Filter Type", juce::StringArray { "Low-pass", "Band-pass", "High-pass" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ATTACK", "Attack", 0.001f, 5.0f, 0.05f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DECAY", "Decay", 0.01f, 5.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUSTAIN", "Sustain", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE", "Release", 0.01f, 5.0f, 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILTER_ENV_ATTACK", "Filter Env Attack", 0.001f, 5.0f, 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILTER_ENV_DECAY", "Filter Env Decay", 0.01f, 5.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILTER_ENV_SUSTAIN", "Filter Env Sustain", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILTER_ENV_RELEASE", "Filter Env Release", 0.01f, 5.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILTER_ENV_AMOUNT", "Filter Env Amount",
        juce::NormalisableRange<float>(-48.0f, 48.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MASTER_VOLUME", "Master Volume", 0.0f, 1.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("MASTER_PAN", "Master Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    for (int n = 1; n <= 2; ++n) {
        const juce::String pre = "LFO" + juce::String(n) + "_";
        const juce::String label = "LFO " + juce::String(n) + " ";
        params.push_back(std::make_unique<juce::AudioParameterBool>(pre + "ON", label + "On", false));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pre + "DEST", label + "Destination",
            juce::StringArray { "Off", "Cutoff", "OSC 1 WT", "OSC 2 WT", "Pitch" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pre + "WAVE", label + "Waveform",
            juce::StringArray { "Sine", "Triangle", "Saw", "Square" }, 0));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "RATE", label + "Rate",
            juce::NormalisableRange<float>(0.05f, 20.0f, 0.01f, 0.5f), 2.0f));
        params.push_back(std::make_unique<juce::AudioParameterBool>(pre + "SYNC", label + "Tempo Sync", false));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(pre + "DIV", label + "Sync Division",
            juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16" }, 2));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(pre + "DEPTH", label + "Depth", 0.0f, 1.0f, 0.2f));
    }

    params.push_back(std::make_unique<juce::AudioParameterChoice>("VOICE_MODE", "Voice Mode", juce::StringArray{"Poly", "Mono", "Legato"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("VOICE_COUNT", "Polyphony", juce::StringArray{"1", "2", "3", "4", "5", "6", "7", "8"}, 7));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("GLIDE_TIME", "Glide Time", 0.0f, 1.0f, 0.05f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("PITCH_RANGE", "Pitch Bend Range", juce::StringArray { "±2 semitones", "±12 semitones", "±24 semitones" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>("REV_MODE", "Reverb Mode", juce::StringArray{"Ambience", "Room", "Hall", "Plate"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REV_SIZE", "Reverb Size", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REV_DECAY", "Reverb Decay", 0.1f, 10.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REV_TONE", "Reverb Tone", 200.0f, 20000.0f, 5000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REV_MIX", "Reverb Mix", 0.0f, 1.0f, 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterBool>("DEL_SYNC", "Delay Sync", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("DEL_NOTE", "Delay Note", juce::StringArray{"1/32", "1/16", "1/8", "1/4"}, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DEL_TONE", "Delay Tone", 200.0f, 20000.0f, 4000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DEL_MIX", "Delay Mix", 0.0f, 1.0f, 0.2f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("SAT_DRIVE", "Saturation Drive", 1.0f, 10.0f, 1.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SAT_MIX", "Saturation Mix", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>("ARP_ON", "Arp On", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("ARP_MODE", "Arp Mode", juce::StringArray{"Up", "Down", "Up/Down", "Random", "As Played"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("ARP_RATE", "Arp Rate", juce::StringArray{"1/32", "1/16", "1/8", "1/4"}, 1));
    params.push_back(std::make_unique<juce::AudioParameterInt>("ARP_OCT", "Arp Octaves", 1, 4, 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ARP_GATE", "Arp Gate", 0.05f, 1.0f, 0.6f));

    return { params.begin(), params.end() };
}

void WavetableSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->prepareToPlay(sampleRate, samplesPerBlock, 2);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
    spec.numChannels = 2;
    reverb.prepare(spec);

    delayLine.setMaximumDelayInSamples((int) (sampleRate * 3.0));
    delayLine.prepare(spec);
    delayLine.reset();
    delayLowpass.fill(0.0f);
    delayTimeSm.reset(sampleRate, 0.15);
    delayTimeSm.setCurrentAndTargetValue((float) (0.3 * sampleRate));

    wavetablesDirty = true;   // таблицы уйдут в голоса в первом processBlock

    arpOut.ensureSize(4096);
    resetArpeggiator();
}

void WavetableSynthAudioProcessor::releaseResources() {}

bool WavetableSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void WavetableSynthAudioProcessor::updateVoiceWavetables() {
    if (!wavetablesDirty.load()) return;
    const juce::SpinLock::ScopedTryLockType lock(wavetableLock);   // аудио-поток никогда не блокируется
    if (!lock.isLocked()) return;

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->setWavetables(wavetable1, wavetable2);
    wavetablesDirty = false;
}

void WavetableSynthAudioProcessor::applyVoiceMode (juce::MidiBuffer& midi, int mode, int voiceCount) {
    voiceModeMidi.clear();
    const bool modeChanged = mode != previousVoiceMode;
    const bool voiceCountChanged = voiceCount != previousVoiceCount;
    if (modeChanged) {
        for (int channel = 1; channel <= 16; ++channel)
            voiceModeMidi.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
        monoHeldNotes.clear();
        polyHeldNotes.clear();
        activeMonoNote = -1;
        previousVoiceMode = mode;
    }
    if (voiceCountChanged) {
        previousVoiceCount = voiceCount;
        while ((int) polyHeldNotes.size() > voiceCount) {
            const auto oldest = polyHeldNotes.front();
            voiceModeMidi.addEvent(juce::MidiMessage::noteOff(oldest.channel, oldest.note), 0);
            polyHeldNotes.erase(polyHeldNotes.begin());
        }
    }

    if (mode == 0) {
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            const int sample = metadata.samplePosition;
            if (message.isNoteOn()) {
                const int note = message.getNoteNumber();
                const int channel = message.getChannel();
                const auto repeated = std::find_if(polyHeldNotes.begin(), polyHeldNotes.end(),
                    [note, channel](const MonoHeldNote& held) { return held.note == note && held.channel == channel; });
                if (repeated != polyHeldNotes.end()) {
                    voiceModeMidi.addEvent(juce::MidiMessage::noteOff(channel, note), sample);
                    polyHeldNotes.erase(repeated);
                }
                if ((int) polyHeldNotes.size() >= voiceCount) {
                    const auto oldest = polyHeldNotes.front();
                    voiceModeMidi.addEvent(juce::MidiMessage::noteOff(oldest.channel, oldest.note), sample);
                    polyHeldNotes.erase(polyHeldNotes.begin());
                }
                polyHeldNotes.push_back({ note, channel, message.getFloatVelocity() });
                voiceModeMidi.addEvent(message, sample);
            } else if (message.isNoteOff()) {
                const int note = message.getNoteNumber();
                const int channel = message.getChannel();
                const auto held = std::find_if(polyHeldNotes.begin(), polyHeldNotes.end(),
                    [note, channel](const MonoHeldNote& item) { return item.note == note && item.channel == channel; });
                if (held != polyHeldNotes.end()) {
                    voiceModeMidi.addEvent(message, sample);
                    polyHeldNotes.erase(held);
                } else if (!voiceCountChanged) {
                    voiceModeMidi.addEvent(message, sample);
                }
            } else {
                voiceModeMidi.addEvent(message, sample);
            }
        }
        midi.swapWith(voiceModeMidi);
        monoHeldNotes.clear();
        activeMonoNote = -1;
        return;
    }
    polyHeldNotes.clear();

    for (const auto metadata : midi) {
        const auto message = metadata.getMessage();
        const int sample = metadata.samplePosition;

        if (message.isNoteOn()) {
            const int note = message.getNoteNumber();
            const int channel = message.getChannel();
            monoHeldNotes.erase(std::remove_if(monoHeldNotes.begin(), monoHeldNotes.end(),
                [note, channel](const MonoHeldNote& held) { return held.note == note && held.channel == channel; }), monoHeldNotes.end());

            voiceModeMidi.addEvent(juce::MidiMessage::allSoundOff(activeMonoChannel), sample);
            if (channel != activeMonoChannel)
                voiceModeMidi.addEvent(juce::MidiMessage::allSoundOff(channel), sample);
            monoHeldNotes.push_back({ note, channel, message.getFloatVelocity() });
            activeMonoNote = note;
            activeMonoChannel = channel;
            voiceModeMidi.addEvent(message, sample);
        } else if (message.isNoteOff()) {
            const int note = message.getNoteNumber();
            const int channel = message.getChannel();
            const auto held = std::find_if(monoHeldNotes.begin(), monoHeldNotes.end(),
                [note, channel](const MonoHeldNote& item) { return item.note == note && item.channel == channel; });
            if (held == monoHeldNotes.end())
                continue;

            const bool releasedActive = note == activeMonoNote && channel == activeMonoChannel;
            monoHeldNotes.erase(held);
            if (!releasedActive) continue;

            if (monoHeldNotes.empty()) {
                voiceModeMidi.addEvent(message, sample);
                activeMonoNote = -1;
            } else {
                const auto next = monoHeldNotes.back();
                voiceModeMidi.addEvent(juce::MidiMessage::allSoundOff(activeMonoChannel), sample);
                voiceModeMidi.addEvent(juce::MidiMessage::noteOn(next.channel, next.note, next.velocity), sample);
                activeMonoNote = next.note;
                activeMonoChannel = next.channel;
            }
        } else {
            voiceModeMidi.addEvent(message, sample);
        }
    }

    midi.swapWith(voiceModeMidi);
}

void WavetableSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    auto get = [this] (const char* id) { return parameters.getRawParameterValue(id)->load(); };

    keyboardState.processNextMidiBuffer(midiMessages, 0, numSamples, true);
    // --- арпеджиатор: превращает удерживаемые ноты в поток нот (MIDI меняется на месте) ---
    processArpeggiator(midiMessages, numSamples);
    const int pitchWheelValue = uiPitchWheelValue.load();
    if (pitchWheelValue != lastUiPitchWheelValue) {
        midiMessages.addEvent(juce::MidiMessage::pitchWheel(1, pitchWheelValue), 0);
        lastUiPitchWheelValue = pitchWheelValue;
    }
    const int modWheelValue = uiModWheelValue.load();
    if (modWheelValue != lastUiModWheelValue) {
        midiMessages.addEvent(juce::MidiMessage::controllerEvent(1, 1, modWheelValue), 0);
        lastUiModWheelValue = modWheelValue;
    }
    applyVoiceMode(midiMessages, (int) std::lround(get("VOICE_MODE")),
                   (int) std::lround(get("VOICE_COUNT")) + 1);

    // --- синтезатор ---
    updateVoiceWavetables();

    VoiceParams vp;
    vp.bpm = (float) currentBpm();
    for (size_t n = 0; n < 2; ++n) {
        const auto& o = oscParams[n];
        auto& d = vp.osc[n];
        d.wave = (int) std::lround(o.wave->load());
        d.wtPos = o.wtPos->load();
        d.level = o.level->load();
        d.octave = (int) std::lround(o.octave->load());
        d.semi = (int) std::lround(o.semi->load());
        d.fineCents = o.fine->load();
        d.phaseDeg = o.phase->load();
        d.pan = o.pan->load();
        d.unison = (int) std::lround(o.unison->load());
        d.detuneCents = o.detune->load();
        d.stereo = o.stereo->load();
        const auto prefix = "OSC" + juce::String((int)n + 1) + "_";
        d.filterOn = get((prefix + "FILTER").toRawUTF8()) > 0.5f;
        d.analogMode = get((prefix + "ANALOG").toRawUTF8()) > 0.5f;
        auto& env = vp.ampEnvelopes[n];
        env.attack = get((prefix + "ENV_ATTACK").toRawUTF8());
        env.decay = get((prefix + "ENV_DECAY").toRawUTF8());
        env.sustain = get((prefix + "ENV_SUSTAIN").toRawUTF8());
        env.release = get((prefix + "ENV_RELEASE").toRawUTF8());
    }
    vp.subWave = (int) get("SUB_WAVE");
    vp.subLevel = get("SUB_LEVEL");
    vp.subFilterOn = get("SUB_FILTER") > 0.5f;
    vp.subAmpEnvelope.attack = get("SUB_ENV_ATTACK");
    vp.subAmpEnvelope.decay = get("SUB_ENV_DECAY");
    vp.subAmpEnvelope.sustain = get("SUB_ENV_SUSTAIN");
    vp.subAmpEnvelope.release = get("SUB_ENV_RELEASE");
    vp.cutoff = get("CUTOFF");
    vp.resonance = get("RESONANCE");
    vp.filterEnabled = get("FILTER_ON") > 0.5f;
    vp.filterType = (int) std::lround(get("FILTER_TYPE"));
    const int pitchRangeIndex = (int) std::lround(get("PITCH_RANGE"));
    vp.pitchBendRange = pitchRangeIndex == 0 ? 2.0f : (pitchRangeIndex == 1 ? 12.0f : 24.0f);
    vp.filterAdsr.attack = get("FILTER_ENV_ATTACK");
    vp.filterAdsr.decay = get("FILTER_ENV_DECAY");
    vp.filterAdsr.sustain = get("FILTER_ENV_SUSTAIN");
    vp.filterAdsr.release = get("FILTER_ENV_RELEASE");
    vp.filterEnvAmountSemitones = get("FILTER_ENV_AMOUNT");
    for (int n = 0; n < 2; ++n) {
        const juce::String pre = "LFO" + juce::String(n + 1) + "_";
        auto& lfo = vp.lfos[(size_t) n];
        lfo.enabled = get((pre + "ON").toRawUTF8()) > 0.5f;
        lfo.destination = (int) std::lround(get((pre + "DEST").toRawUTF8()));
        lfo.waveform = (int) std::lround(get((pre + "WAVE").toRawUTF8()));
        lfo.rateHz = get((pre + "RATE").toRawUTF8());
        lfo.tempoSync = get((pre + "SYNC").toRawUTF8()) > 0.5f;
        lfo.syncDivision = (int) std::lround(get((pre + "DIV").toRawUTF8()));
        lfo.depth = get((pre + "DEPTH").toRawUTF8());
    }
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->setParams(vp);

    synth.renderNextBlock(buffer, midiMessages, 0, numSamples);

    // --- сатурация ---
    const float satDrive = get("SAT_DRIVE");
    const float satMix = get("SAT_MIX");
    for (int ch = 0; ch < numChannels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
            const float clean = data[i];
            data[i] = clean * (1.0f - satMix) + std::tanh(clean * satDrive) * satMix;
        }
    }

    // --- делэй (синхронизация с темпом хоста; без синхронизации 300 мс) ---
    const double bpm = currentBpm();

    static constexpr float noteBeats[] = { 0.125f, 0.25f, 0.5f, 1.0f };
    const bool sync = get("DEL_SYNC") > 0.5f;
    const int noteIdx = juce::jlimit(0, 3, (int) get("DEL_NOTE"));
    const double delaySec = juce::jlimit(0.001, 2.9, sync ? noteBeats[noteIdx] * 60.0 / bpm : 0.3);
    delayTimeSm.setTargetValue((float) (delaySec * currentSampleRate));

    const float delMix = get("DEL_MIX");
    const float toneCoef = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * get("DEL_TONE") / (float) currentSampleRate);
    const int fxCh = juce::jmin(numChannels, 2);
    constexpr float feedback = 0.4f;

    for (int i = 0; i < numSamples; ++i) {
        const float d = delayTimeSm.getNextValue();
        for (int ch = 0; ch < fxCh; ++ch) {
            float* data = buffer.getWritePointer(ch);
            const float in = data[i];
            const float delayed = delayLine.popSample(ch, d, true);
            auto& lp = delayLowpass[(size_t) ch];
            lp += toneCoef * (delayed - lp);
            delayLine.pushSample(ch, in + lp * feedback);
            data[i] = in + lp * delMix;
        }
    }

    // --- ревербератор: Size -> roomSize, Tone -> damping (выше тон = меньше демпфирования) ---
    const float revMix = get("REV_MIX");
    juce::dsp::Reverb::Parameters revParams;
    revParams.roomSize = get("REV_SIZE");
    revParams.damping = juce::jlimit(0.0f, 1.0f, 1.0f - std::log(get("REV_TONE") / 200.0f) / std::log(100.0f));
    revParams.wetLevel = revMix;
    revParams.dryLevel = 1.0f - revMix;
    revParams.width = 1.0f;
    revParams.freezeMode = 0.0f;
    reverb.setParameters(revParams);

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    reverb.process(context);

    // Master volume and stereo balance are applied after the effects chain.
    const float masterVolume = get("MASTER_VOLUME");
    const float masterPan = get("MASTER_PAN");
    const float panLeft = 1.0f - juce::jmax(0.0f, masterPan);
    const float panRight = 1.0f + juce::jmin(0.0f, masterPan);
    float peakLeft = 0.0f, peakRight = 0.0f;
    float* leftData = buffer.getWritePointer(0);
    float* rightData = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < numSamples; ++i) {
        if (numChannels == 1) {
            float& sample = leftData[i];
            sample *= masterVolume;
            peakLeft = peakRight = juce::jmax(peakLeft, std::abs(sample));
        } else {
            float& left = leftData[i];
            float& right = rightData[i];
            left *= masterVolume * panLeft;
            right *= masterVolume * panRight;
            peakLeft = juce::jmax(peakLeft, std::abs(left));
            peakRight = juce::jmax(peakRight, std::abs(right));
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.getWritePointer(ch)[i] *= masterVolume;
        }
    }
    const float meterDecay = std::exp(-(float) numSamples / (float) (currentSampleRate * 0.35));
    outputPeaks[0].store(juce::jmax(peakLeft, outputPeaks[0].load() * meterDecay));
    outputPeaks[1].store(juce::jmax(peakRight, outputPeaks[1].load() * meterDecay));

    // --- осциллограф: скользящее окно последних 512 сэмплов ---
    const int scopeSize = (int) oscilloscopeBuffer.size();
    if (numSamples > 0 && scopeSize > 0) {
        const int k = juce::jmin(numSamples, scopeSize);
        std::memmove(oscilloscopeBuffer.data(), oscilloscopeBuffer.data() + k, (size_t) (scopeSize - k) * sizeof(float));
        std::memcpy(oscilloscopeBuffer.data() + (scopeSize - k), buffer.getReadPointer(0) + (numSamples - k), (size_t) k * sizeof(float));
    }
}

double WavetableSynthAudioProcessor::currentBpm() const {
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 20.0) return *b;
    return 120.0;
}

void WavetableSynthAudioProcessor::resetArpeggiator() {
    heldNotes.clear();
    arpCountdown = 0.0;
    arpGateCountdown = 0;
    arpStepIndex = 0;
    arpCurrentNote = -1;
    arpWasOn = false;
}

// Следующая нота арпеджио. Режимы: 0 Up, 1 Down, 2 Up/Down, 3 Random, 4 As Played.
int WavetableSynthAudioProcessor::nextArpNote(int mode, int octaves) {
    arpSorted.assign(heldNotes.begin(), heldNotes.end());
    std::sort(arpSorted.begin(), arpSorted.end());

    const auto& base = (mode == 4) ? heldNotes : arpSorted;
    arpSeq.clear();
    for (int o = 0; o < octaves; ++o)
        for (int n : base)
            if (n + 12 * o <= 127) arpSeq.push_back(n + 12 * o);
    if (arpSeq.empty()) return -1;

    if (mode == 1) {
        std::reverse(arpSeq.begin(), arpSeq.end());
    } else if (mode == 2 && arpSeq.size() > 2) {
        for (size_t i = arpSeq.size() - 2; i >= 1; --i) {
            const int v = arpSeq[i];
            arpSeq.push_back(v);
        }
    } else if (mode == 3) {
        return arpSeq[(size_t) arpRandom.nextInt((int) arpSeq.size())];
    }

    const size_t idx = (size_t) arpStepIndex % arpSeq.size();
    ++arpStepIndex;
    return arpSeq[idx];
}

void WavetableSynthAudioProcessor::processArpeggiator(juce::MidiBuffer& midi, int numSamples) {
    auto addHeld = [this] (int n) {
        if (heldNotes.size() < 128 && std::find(heldNotes.begin(), heldNotes.end(), n) == heldNotes.end())
            heldNotes.push_back(n);
    };
    auto removeHeld = [this] (int n) {
        heldNotes.erase(std::remove(heldNotes.begin(), heldNotes.end(), n), heldNotes.end());
    };

    const bool on = param("ARP_ON") > 0.5f;

    if (!on) {
        // Клавиши отслеживаем и при выключенном арпеджиаторе, чтобы его можно было включить на удерживаемом аккорде.
        for (const auto meta : midi) {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) addHeld(msg.getNoteNumber());
            else if (msg.isNoteOff()) removeHeld(msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff()) heldNotes.clear();
        }
        if (arpWasOn) {   // только что выключили: гасим ноту арпеджиатора
            midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            arpCurrentNote = -1;
            arpGateCountdown = 0;
            arpWasOn = false;
        }
        return;
    }

    arpOut.clear();
    if (!arpWasOn) {   // только что включили: гасим обычные ноты, арпеджио стартует сразу
        arpOut.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        arpWasOn = true;
        arpCountdown = 0.0;
        arpStepIndex = 0;
        arpCurrentNote = -1;
    }

    static constexpr double rateBeats[] = { 0.125, 0.25, 0.5, 1.0 };
    const int rateIdx = juce::jlimit(0, 3, (int) param("ARP_RATE"));
    const double samplesPerStep = rateBeats[rateIdx] * 60.0 / currentBpm() * currentSampleRate;
    const int gateSamples = juce::jmax(1, (int) (samplesPerStep * param("ARP_GATE")));
    const int mode = (int) param("ARP_MODE");
    const int octaves = juce::jlimit(1, 4, (int) param("ARP_OCT"));

    auto it = midi.begin();
    const auto end = midi.end();

    for (int s = 0; s < numSamples; ++s) {
        while (it != end && (*it).samplePosition <= s) {
            const auto msg = (*it).getMessage();
            if (msg.isNoteOn()) {
                // Первая нота аккорда: ждём 10 мс, чтобы остальные ноты аккорда успели нажаться и попали в арпеджио
                if (heldNotes.empty()) { arpCountdown = 0.010 * currentSampleRate; arpStepIndex = 0; }
                addHeld(msg.getNoteNumber());
                arpVelocity = msg.getVelocity();
            } else if (msg.isNoteOff()) {
                removeHeld(msg.getNoteNumber());
            } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
                heldNotes.clear();
            } else {
                arpOut.addEvent(msg, s);   // CC, pitch bend и прочее пропускаем как есть
            }
            ++it;
        }

        if (arpGateCountdown > 0 && --arpGateCountdown == 0 && arpCurrentNote >= 0) {
            arpOut.addEvent(juce::MidiMessage::noteOff(1, arpCurrentNote), s);
            arpCurrentNote = -1;
        }

        if (heldNotes.empty()) {
            if (arpCurrentNote >= 0) {
                arpOut.addEvent(juce::MidiMessage::noteOff(1, arpCurrentNote), s);
                arpCurrentNote = -1;
                arpGateCountdown = 0;
            }
            arpCountdown = 0.0;
            continue;
        }

        if (arpCountdown <= 0.0) {
            const int note = nextArpNote(mode, octaves);
            if (arpCurrentNote >= 0) {
                arpOut.addEvent(juce::MidiMessage::noteOff(1, arpCurrentNote), s);
                arpCurrentNote = -1;
            }
            if (note >= 0) {
                arpOut.addEvent(juce::MidiMessage::noteOn(1, note, arpVelocity), s);
                arpCurrentNote = note;
                arpGateCountdown = gateSamples;
            }
            arpCountdown += samplesPerStep;
        }
        arpCountdown -= 1.0;
    }

    midi.swapWith(arpOut);
}

void WavetableSynthAudioProcessor::setWavetableStatus(int idx, const juce::String& text, const juce::File& loaded) {
    const juce::CriticalSection::ScopedLockType lock(statusLock);
    statusText[(size_t) idx] = text;
    if (loaded != juce::File()) loadedFile[(size_t) idx] = loaded;
}

juce::String WavetableSynthAudioProcessor::getWavetableStatus(int oscIndex) const {
    const juce::CriticalSection::ScopedLockType lock(statusLock);
    return statusText[oscIndex == 1 ? 0 : 1];
}

juce::File WavetableSynthAudioProcessor::getWavetableFile(int oscIndex) const {
    const juce::CriticalSection::ScopedLockType lock(statusLock);
    return loadedFile[oscIndex == 1 ? 0 : 1];
}

bool WavetableSynthAudioProcessor::loadCustomWavetable(const juce::File& file, int oscIndex) {
    const int idx = (oscIndex == 1) ? 0 : 1;
    auto fail = [&] (const juce::String& why) {
        setWavetableStatus(idx, file.getFileName() + ": " + why + " (previous waveform kept)", juce::File());
        return false;
    };

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return fail("cannot read this file");
    if (reader->lengthInSamples <= 0 || reader->numChannels == 0) return fail("file is empty");

    const int totalSamples = (int) std::min<juce::int64>(reader->lengthInSamples, (juce::int64) 1 << 24);
    juce::AudioBuffer<float> temp((int) reader->numChannels, totalSamples);
    temp.clear();
    if (!reader->read(&temp, 0, totalSamples, 0, true, true)) return fail("read error");

    // Сводим все каналы в моно (раньше брался только левый)
    std::vector<float> mono((size_t) totalSamples, 0.0f);
    const float chGain = 1.0f / (float) temp.getNumChannels();
    for (int ch = 0; ch < temp.getNumChannels(); ++ch) {
        const float* src = temp.getReadPointer(ch);
        for (int i = 0; i < totalSamples; ++i) mono[(size_t) i] += src[i] * chGain;
    }
    for (float x : mono)
        if (!std::isfinite(x)) return fail("file contains invalid data");

    auto built = wavetable::build(mono.data(), totalSamples);
    if (built.table == nullptr) return fail(juce::String(built.description));

    std::shared_ptr<const WaveTable> toFree;
    {
        const juce::SpinLock::ScopedLockType lock(wavetableLock);
        auto& slot = (idx == 0) ? wavetable1 : wavetable2;
        retiredTables.push_back(slot);
        slot = built.table;
        if (retiredTables.size() > 4) {
            toFree = retiredTables.front();
            retiredTables.erase(retiredTables.begin());
        }
    }
    wavetablesDirty = true;

    setWavetableStatus(idx, file.getFileNameWithoutExtension() + " (" + juce::String(built.description) + ")", file);
    return true;
}

void WavetableSynthAudioProcessor::loadWavetableAsync(const juce::File& file, int oscIndex) {
    loadPool.addJob([this, file, oscIndex] { loadCustomWavetable(file, oscIndex); });
}

void WavetableSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData) {
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr) return;
    // Какие файлы загружены в осцилляторы — сохраняем в пресет, чтобы после перезагрузки проекта они вернулись
    xml->setAttribute("osc1File", getWavetableFile(1).getFullPathName());
    xml->setAttribute("osc2File", getWavetableFile(2).getFullPathName());
    copyXmlToBinary (*xml, destData);
}

void WavetableSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState == nullptr || !xmlState->hasTagName (parameters.state.getType())) return;

    parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
    for (int osc = 1; osc <= 2; ++osc) {
        const auto path = xmlState->getStringAttribute(osc == 1 ? "osc1File" : "osc2File");
        if (path.isEmpty()) continue;
        const juce::File f(path);
        if (f.existsAsFile()) loadWavetableAsync(f, osc);
        else setWavetableStatus(osc == 1 ? 0 : 1, f.getFileName() + ": file not found", juce::File());
    }
}

juce::AudioProcessorEditor* WavetableSynthAudioProcessor::createEditor() {
    return new WavetableSynthAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new WavetableSynthAudioProcessor();
}
