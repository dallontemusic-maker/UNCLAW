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
};

struct VoiceParams {
    std::array<OscSettings, 2> osc;
    int subWave = 2;
    float subLevel = 0.0f;
    float cutoff = 1000.0f, resonance = 1.0f;
    juce::ADSR::Parameters adsr;
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
        filter.reset();
        adsr.noteOn();
    }

    void stopNote (float, bool allowTailOff) override {
        adsr.noteOff();
        if (!allowTailOff || !adsr.isActive()) clearCurrentNote();
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void prepareToPlay(double sampleRate, int samplesPerBlock, int outputChannels) {
        currentSampleRate = sampleRate;
        smoothCoef = 1.0f - std::exp(-1.0f / (0.005f * (float) sampleRate));   // сглаживание 5 мс
        for (auto& u : units)
            for (auto& v : u.voices) v.prepare(sampleRate);
        sub.prepare(sampleRate);
        adsr.setSampleRate(sampleRate);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32) samplesPerBlock;
        spec.numChannels = (juce::uint32) juce::jmax(2, outputChannels);
        filter.prepare(spec);
        filter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
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
        }
        updateFrequencies();

        sub.setSource(toSubSource(p.subWave));
        subLevel = p.subLevel;
        filter.setCutoffFrequency(juce::jlimit(20.0f, (float)(0.45 * currentSampleRate), p.cutoff));
        filter.setResonance(p.resonance);
        adsr.setParameters(p.adsr);
    }

    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override {
        if (!isVoiceActive()) return;
        const int numCh = outputBuffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i) {
            float left = 0.0f, right = 0.0f;

            for (auto& u : units) {
                u.levelCur += (u.s.level - u.levelCur) * smoothCoef;
                u.panCur += (u.s.pan - u.panCur) * smoothCoef;
                u.stereoCur += (u.s.stereo - u.stereoCur) * smoothCoef;
                const float gain = u.levelCur * u.norm;

                for (int k = 0; k < u.n; ++k) {
                    const float x = u.voices[(size_t) k].processSample() * gain;
                    // позиция голоса: Pan осциллятора + разброс юнисона * Stereo
                    const float pos = juce::jlimit(-1.0f, 1.0f, u.panCur + u.spread[(size_t) k] * u.stereoCur);
                    left += x * (pos > 0.0f ? 1.0f - pos : 1.0f);
                    right += x * (pos < 0.0f ? 1.0f + pos : 1.0f);
                }
            }
            left *= 0.5f;
            right *= 0.5f;

            const float subSample = subLevel * sub.processSample();
            left += subSample;
            right += subSample;

            const float env = adsr.getNextSample();
            const float gainOut = velocityGain * 0.2f;
            const float outL = filter.processSample(0, left * env) * gainOut;
            const float outR = filter.processSample(1, right * env) * gainOut;

            if (numCh == 1) {
                outputBuffer.addSample(0, startSample + i, 0.5f * (outL + outR));
            } else {
                outputBuffer.addSample(0, startSample + i, outL);
                outputBuffer.addSample(1, startSample + i, outR);
                for (int ch = 2; ch < numCh; ++ch)
                    outputBuffer.addSample(ch, startSample + i, 0.5f * (outL + outR));
            }

            if (!adsr.isActive()) {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    struct OscUnit {
        std::array<WavetableOscillator, kMaxUnison> voices;
        std::array<float, kMaxUnison> spread {};
        OscSettings s;
        int n = 1;
        float norm = 1.0f;
        float levelCur = 1.0f, panCur = 0.0f, stereoCur = 0.7f;
    };

    // Высота каждого голоса: нота + Octave + Interval + Fine + разброс Detune (в центах)
    void updateFrequencies() {
        for (auto& u : units) {
            const float baseCents = (float) (u.s.octave * 1200 + u.s.semi * 100) + u.s.fineCents;
            for (int k = 0; k < u.n; ++k) {
                const float cents = baseCents + u.spread[(size_t) k] * u.s.detuneCents;
                u.voices[(size_t) k].setFrequency(baseFreq * std::exp2(cents / 1200.0f));
            }
        }
    }

    std::array<OscUnit, 2> units;
    WavetableOscillator sub;
    float subLevel = 0.0f;
    juce::ADSR adsr;
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::Random random;
    double currentSampleRate = 44100.0;
    float smoothCoef = 0.01f;
    float baseFreq = 440.0f;
    float velocityGain = 1.0f;
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
    heldNotes.reserve(128);
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
    }
    params.push_back(std::make_unique<juce::AudioParameterChoice>("SUB_WAVE", "Sub Wave", juce::StringArray{"Sine", "Square", "Noise"}, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUB_LEVEL", "Sub Level", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CUTOFF", "Filter Cutoff", juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f), 8000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RESONANCE", "Filter Resonance", 0.1f, 10.0f, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ATTACK", "Attack", 0.001f, 5.0f, 0.05f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DECAY", "Decay", 0.01f, 5.0f, 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SUSTAIN", "Sustain", 0.0f, 1.0f, 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RELEASE", "Release", 0.01f, 5.0f, 0.4f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>("VOICE_MODE", "Voice Mode", juce::StringArray{"Poly", "Mono", "Legato"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("GLIDE_TIME", "Glide Time", 0.0f, 1.0f, 0.05f));

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

void WavetableSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    buffer.clear();

    auto get = [this] (const char* id) { return parameters.getRawParameterValue(id)->load(); };

    // --- арпеджиатор: превращает удерживаемые ноты в поток нот (MIDI меняется на месте) ---
    processArpeggiator(midiMessages, numSamples);

    // --- синтезатор ---
    updateVoiceWavetables();

    VoiceParams vp;
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
    }
    vp.subWave = (int) get("SUB_WAVE");
    vp.subLevel = get("SUB_LEVEL");
    vp.cutoff = get("CUTOFF");
    vp.resonance = get("RESONANCE");
    vp.adsr.attack = get("ATTACK");
    vp.adsr.decay = get("DECAY");
    vp.adsr.sustain = get("SUSTAIN");
    vp.adsr.release = get("RELEASE");
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

    // --- осциллограф: скользящее окно последних 512 сэмплов ---
    const int scopeSize = oscilloscopeSize;
    if (numSamples > 0 && scopeSize > 0) {
        const int k = juce::jmin(numSamples, scopeSize);
        for (int i = 0; i < scopeSize - k; ++i)
            oscilloscopeBuffer[(size_t) i].store(oscilloscopeBuffer[(size_t) (i + k)].load(std::memory_order_relaxed), std::memory_order_relaxed);
        const float* source = buffer.getReadPointer(0) + (numSamples - k);
        for (int i = 0; i < k; ++i)
            oscilloscopeBuffer[(size_t) (scopeSize - k + i)].store(source[i], std::memory_order_relaxed);
    }
}

void WavetableSynthAudioProcessor::copyOscilloscopeBuffer(float* destination, int count) const {
    if (destination == nullptr || count <= 0) return;
    const int n = juce::jmin(count, oscilloscopeSize);
    const int start = oscilloscopeSize - n;
    for (int i = 0; i < n; ++i)
        destination[i] = oscilloscopeBuffer[(size_t) (start + i)].load(std::memory_order_relaxed);
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
