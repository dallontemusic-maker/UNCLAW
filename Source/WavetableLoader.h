#pragma once
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "WavetableOscillator.h"

// Превращает аудиоданные (моно) в вейвтейбл из кадров по 2048 сэмплов.
// Задача: любой "разумный" WAV должен давать слышимый звук при Wavetable Pos = 0.
namespace wavetable {

constexpr int kFrameSize = 2048;
constexpr int kMaxFrames = 512;
constexpr float kSilence = 1.0e-3f;     // -60 dBFS: кадры тише считаем тишиной
constexpr float kTargetPeak = 0.95f;

struct BuildResult {
    std::shared_ptr<const WaveTable> table;   // nullptr => ошибка, причина в description
    std::string description;
};

inline float peakOf(const std::vector<float>& f) {
    float p = 0.0f;
    for (float x : f) p = std::max(p, std::abs(x));
    return p;
}

inline BuildResult build(const float* data, int numSamples) {
    BuildResult result;
    if (data == nullptr || numSamples <= 0) {
        result.description = "file is empty";
        return result;
    }

    std::vector<std::vector<float>> frames;
    bool singleCycle = false;

    if (numSamples < kFrameSize || (numSamples % kFrameSize != 0 && numSamples <= 4 * kFrameSize)) {
        // Один период произвольной длины (например, 600 сэмплов): растягиваем до 2048
        std::vector<float> f((size_t) kFrameSize);
        for (int i = 0; i < kFrameSize; ++i) {
            const double pos = (double) i * numSamples / kFrameSize;
            const int i0 = (int) pos;
            const int i1 = (i0 + 1) % numSamples;
            const float frac = (float) (pos - i0);
            f[(size_t) i] = data[i0] + frac * (data[i1] - data[i0]);
        }
        frames.push_back(std::move(f));
        singleCycle = true;
    } else {
        // Вейвтейбл из кадров по 2048 (хвост, не кратный кадру, отбрасываем)
        const int n = std::min(numSamples / kFrameSize, kMaxFrames);
        for (int t = 0; t < n; ++t)
            frames.emplace_back(data + t * kFrameSize, data + (t + 1) * kFrameSize);
    }

    // Убираем постоянную составляющую в каждом кадре
    for (auto& f : frames) {
        double mean = 0.0;
        for (float x : f) mean += x;
        mean /= (double) f.size();
        for (float& x : f) x -= (float) mean;
    }

    // Срезаем тишину в начале и в конце: иначе при позиции 0 слышна пустота
    size_t first = 0, last = frames.size();
    while (first < last && peakOf(frames[first]) < kSilence) ++first;
    while (last > first && peakOf(frames[last - 1]) < kSilence) --last;
    if (first == last) {
        result.description = "file is silent";
        return result;
    }

    auto table = std::make_shared<WaveTable>(std::make_move_iterator(frames.begin() + (long) first),
                                             std::make_move_iterator(frames.begin() + (long) last));

    // Нормализуем каждый кадр по пику: громкость не зависит от того, как записан файл
    for (auto& f : *table) {
        const float p = peakOf(f);
        if (p < kSilence) {
            std::fill(f.begin(), f.end(), 0.0f);   // тишина посреди таблицы остаётся тишиной
        } else {
            const float g = kTargetPeak / p;
            for (float& x : f) x *= g;
        }
    }

    result.description = singleCycle ? "single cycle" : std::to_string(table->size()) + (table->size() == 1 ? " frame" : " frames");
    if (first > 0) result.description += ", leading silence skipped";
    result.table = std::move(table);
    return result;
}

} // namespace wavetable
