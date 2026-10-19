#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <memory>
#include <vector>

// Вейвтейбл: набор кадров одинаковой длины. Используется через shared_ptr,
// чтобы GUI-поток мог безопасно подменять таблицу, пока аудио-поток её читает.
using WaveTable = std::vector<std::vector<float>>;

class WavetableOscillator {
public:
    // Источник сигнала: вейвтейбл (встроенный или загруженный из WAV) либо одна из базовых форм.
    enum class Source { Wavetable = 0, Sine, Triangle, Saw, Square, Noise };

    void prepare(double sampleRate) {
        currentSampleRate = sampleRate;
        phase = 0.0f;
    }

    void loadTable(std::shared_ptr<const WaveTable> newTable) {
        if (newTable != nullptr && !newTable->empty() && !(*newTable)[0].empty()) {
            wavetable = std::move(newTable);
            tableSize = (*wavetable)[0].size();
        }
    }

    void setSource(Source s) { source = s; }
    void setFrequency(float freq) { frequency = freq; }
    void setWavetablePosition(float pos) { wtPosition = juce::jlimit(0.0f, 1.0f, pos); }
    // Начальная фаза в долях периода (0..1)
    void resetPhase(float startPhase = 0.0f) { phase = startPhase - std::floor(startPhase); }

    float processSample() {
        const float dt = juce::jlimit(1.0e-6f, 0.45f, frequency / (float) currentSampleRate);
        float out = 0.0f;

        switch (source) {
            case Source::Wavetable:
                out = readWavetable();
                break;
            case Source::Sine:
                out = std::sin(juce::MathConstants<float>::twoPi * phase);
                break;
            case Source::Triangle: {
                float x = phase + 0.25f;
                x -= std::floor(x);
                out = 1.0f - 4.0f * std::abs(x - 0.5f);   // 0 -> +1 -> 0 -> -1
                break;
            }
            case Source::Saw:
                out = 2.0f * phase - 1.0f - polyBlep(phase, dt);
                break;
            case Source::Square: {
                float p2 = phase + 0.5f;
                if (p2 >= 1.0f) p2 -= 1.0f;
                out = (phase < 0.5f ? 1.0f : -1.0f) + polyBlep(phase, dt) - polyBlep(p2, dt);
                break;
            }
            case Source::Noise:
                out = rng.nextFloat() * 2.0f - 1.0f;   // белый шум
                break;
        }

        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }

private:
    // PolyBLEP: сглаживает разрывы пилы/квадрата и убирает основную часть алиасинга
    static float polyBlep(float t, float dt) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt) {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    float readWavetable() const {
        if (wavetable == nullptr || tableSize == 0) return 0.0f;
        const auto& wt = *wavetable;

        const float scaledPos = wtPosition * (float)(wt.size() - 1);
        const int tableIndex1 = juce::jlimit(0, (int)wt.size() - 1, (int)scaledPos);
        const int tableIndex2 = juce::jmin(tableIndex1 + 1, (int)wt.size() - 1);
        const float tableFrac = scaledPos - (float)tableIndex1;

        const float readIndex = phase * (float)tableSize;
        const int index1 = juce::jmin((int)readIndex, (int)tableSize - 1);
        const int index2 = (index1 + 1) % (int)tableSize;
        const float frac = readIndex - (float)index1;

        const auto& t1 = wt[(size_t)tableIndex1];
        const auto& t2 = wt[(size_t)tableIndex2];
        const float val1 = t1[(size_t)index1] + frac * (t1[(size_t)index2] - t1[(size_t)index1]);
        const float val2 = t2[(size_t)index1] + frac * (t2[(size_t)index2] - t2[(size_t)index1]);
        return val1 + tableFrac * (val2 - val1);
    }

    double currentSampleRate = 44100.0;
    float frequency = 440.0f;
    float phase = 0.0f;
    float wtPosition = 0.0f;
    Source source = Source::Wavetable;
    juce::Random rng;

    std::shared_ptr<const WaveTable> wavetable;
    size_t tableSize = 0;
};
