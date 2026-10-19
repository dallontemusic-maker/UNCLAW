#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

// Гибридный источник (третий после OSC 1 и OSC 2): саб-осциллятор, цветной шум или сэмплер.
namespace hybrid {

enum class Mode { Sub = 0, Noise, Sample };
enum class NoiseColour { White = 0, Pink, Brown };
enum class Route { Filter = 0, Direct, Dry };   // Dry — мимо фильтра и эффектов, сразу в мастер

// Сэмпл неизменяем после загрузки и делится между голосами через shared_ptr (как вейвтейблы).
struct Sample {
    juce::AudioBuffer<float> audio;   // 1 или 2 канала
    double sampleRate = 44100.0;
    std::vector<float> peaks;         // огибающая для экрана: пик каждого из kPeakCount отрезков
    juce::String name;

    static constexpr int kPeakCount = 256;
    double lengthSeconds() const { return sampleRate > 0.0 ? audio.getNumSamples() / sampleRate : 0.0; }
};

struct Settings {
    Mode mode = Mode::Sub;
    int subShape = 0;            // 0 — синус, 1 — квадрат
    int subOctave = -1;          // -1 или -2 октавы от ноты
    float fineCents = 0.0f;
    float pan = 0.0f;
    float level = 0.0f;
    bool enabled = true;
    Route route = Route::Filter;

    NoiseColour colour = NoiseColour::White;
    float tiltDb = 0.0f;         // наклон спектра: + ярче, − темнее
    float noiseStereo = 0.5f;    // 0 — моно, 1 — независимые L/R

    int pitchSemitones = 0;      // транспонирование сэмпла
    float start = 0.0f;          // точка старта, доля длины 0..1
    bool loop = false, reverse = false, keytrack = true;
    int rootNote = 60;           // нота, на которой сэмпл звучит в оригинальной высоте
};

// Белый / розовый (фильтр Пола Келлета, −3 дБ/окт) / коричневый (интегрированный белый, −6 дБ/окт).
class ColouredNoise {
public:
    float next(juce::Random& rng, NoiseColour colour) {
        const float white = rng.nextFloat() * 2.0f - 1.0f;
        switch (colour) {
            case NoiseColour::Pink: {
                b0 = 0.99886f * b0 + white * 0.0555179f;
                b1 = 0.99332f * b1 + white * 0.0750759f;
                b2 = 0.96900f * b2 + white * 0.1538520f;
                b3 = 0.86650f * b3 + white * 0.3104856f;
                b4 = 0.55000f * b4 + white * 0.5329522f;
                b5 = -0.7616f * b5 - white * 0.0168980f;
                const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
                b6 = white * 0.115926f;
                return out * 0.11f;
            }
            case NoiseColour::Brown:
                brown = (brown + 0.02f * white) / 1.02f;
                return brown * 3.5f;
            case NoiseColour::White:
            default:
                return white;
        }
    }

private:
    float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, brown = 0;
};

// Проигрывание сэмпла с переменной скоростью: кубическая интерполяция Эрмита, loop и reverse.
class SamplePlayer {
public:
    void start(const Sample* s, float startFraction, bool reverse) {
        sample = s;
        const int len = s != nullptr ? s->audio.getNumSamples() : 0;
        active = len > 1;
        if (!active) return;
        const double last = (double) (len - 1);
        const double offset = juce::jlimit(0.0, 1.0, (double) startFraction) * last;
        pos = reverse ? last - offset : offset;
        loopPoint = pos;
    }

    void stop() { active = false; }

    // increment — шаг чтения в сэмплах файла на один сэмпл вывода
    void process(float& left, float& right, double increment, bool loop, bool reverse) {
        left = right = 0.0f;
        if (!active || sample == nullptr) return;
        const auto& audio = sample->audio;
        const int len = audio.getNumSamples();
        if (len < 2 || pos < 0.0 || pos > (double) (len - 1)) { active = false; return; }

        left = read(audio.getReadPointer(0), len);
        right = audio.getNumChannels() > 1 ? read(audio.getReadPointer(1), len) : left;

        const double last = (double) (len - 1);
        if (!reverse) {
            pos += increment;
            if (pos > last) {
                const double span = last - loopPoint;
                if (loop && span > 1.0) pos = loopPoint + std::fmod(pos - last, span);
                else active = false;
            }
        } else {
            pos -= increment;
            if (pos < 0.0) {
                const double span = loopPoint;
                if (loop && span > 1.0) pos = loopPoint - std::fmod(-pos, span);
                else active = false;
            }
        }
    }

private:
    float read(const float* data, int len) const {
        const int i1 = (int) pos;
        const float t = (float) (pos - (double) i1);
        const float x0 = data[juce::jmax(0, i1 - 1)];
        const float x1 = data[i1];
        const float x2 = data[juce::jmin(len - 1, i1 + 1)];
        const float x3 = data[juce::jmin(len - 1, i1 + 2)];
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * t + c2) * t + c1) * t + x1;
    }

    const Sample* sample = nullptr;
    double pos = 0.0, loopPoint = 0.0;
    bool active = false;
};

// Наклон спектра: однополюсный разделитель на ~700 Гц, низ и верх с противоположным усилением.
class Tilt {
public:
    void prepare(double sampleRate) {
        coef = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * 700.0f / (float) sampleRate);
        low.fill(0.0f);
    }
    void setTilt(float db) {
        high = juce::Decibels::decibelsToGain(0.5f * db);
        lowGain = juce::Decibels::decibelsToGain(-0.5f * db);
    }
    float process(int channel, float x) {
        auto& lp = low[(size_t) channel];
        lp += coef * (x - lp);
        return lp * lowGain + (x - lp) * high;
    }

private:
    float coef = 0.1f, high = 1.0f, lowGain = 1.0f;
    std::array<float, 2> low {};
};

inline juce::String noteName(int note) {
    return juce::MidiMessage::getMidiNoteName(juce::jlimit(0, 127, note), true, true, 3);
}

} // namespace hybrid
