#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "UnclawLookAndFeel.h"
#include <array>
#include <vector>

// Осциллограф в стиле люминофорного экрана: прошлые кадры гаснут постепенно, у линии ореол и светлое ядро.
class OscilloscopeDisplay : public juce::Component, public juce::Timer {
public:
    OscilloscopeDisplay(std::vector<float>& bufferRef) : audioBuffer(bufferRef) {
        startTimerHz(30);
    }

    void timerCallback() override {
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat();
        unclaw::drawScreen(g, area, 6, 6);
        const auto accent = unclaw::colours::amber;

        // перекрестье с делениями
        g.setColour(juce::Colours::white.withAlpha(0.11f));
        g.drawLine(area.getCentreX(), area.getY() + 3.0f, area.getCentreX(), area.getBottom() - 3.0f, 1.0f);
        g.drawLine(area.getX() + 3.0f, area.getCentreY(), area.getRight() - 3.0f, area.getCentreY(), 1.0f);
        for (int i = 1; i < 24; ++i) {
            const float x = area.getX() + area.getWidth() * (float) i / 24.0f;
            const float y = area.getY() + area.getHeight() * (float) i / 24.0f;
            g.drawLine(x, area.getCentreY() - 2.0f, x, area.getCentreY() + 2.0f, 1.0f);
            g.drawLine(area.getCentreX() - 2.0f, y, area.getCentreX() + 2.0f, y, 1.0f);
        }

        // новый кадр сдвигает историю: каждый прошлый кадр рисуется бледнее
        juce::Path wavePath;
        float peak = 0.0f;
        if (!audioBuffer.empty()) {
            const float width = area.getWidth() - 4.0f;
            const float centerY = area.getCentreY();
            const size_t bufferSize = audioBuffer.size();
            for (size_t i = 0; i < bufferSize; ++i) {
                const float sample = juce::jlimit(-1.2f, 1.2f, audioBuffer[i]);
                peak = juce::jmax(peak, std::abs(sample));
                const float x = 2.0f + (float) i / (float) (bufferSize - 1) * width;
                const float y = centerY - sample * area.getHeight() * 0.5f * 0.78f;
                if (i == 0) wavePath.startNewSubPath(x, y); else wavePath.lineTo(x, y);
            }
        }
        for (size_t i = history.size() - 1; i > 0; --i) history[i] = history[i - 1];
        history[0] = wavePath;

        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(area.reduced(1.0f).toNearestInt());
        static constexpr float ghostAlpha[] { 0.0f, 0.18f, 0.11f, 0.07f };
        for (size_t i = history.size() - 1; i > 0; --i) {
            g.setColour(accent.withAlpha(ghostAlpha[i]));
            g.strokePath(history[i], juce::PathStrokeType(1.0f));
        }
        unclaw::strokeGlow(g, wavePath, accent, 1.1f, { { 5.5f, 0.10f }, { 2.4f, 0.45f } }, juce::Colour(0xfffff1c2));

        // виньетка по краям экрана
        g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack, area.getCentreX(), area.getCentreY(),
                                               juce::Colours::black.withAlpha(0.5f), area.getX(), area.getY(), true));
        g.fillRoundedRectangle(area, 4.0f);

        g.setFont(unclaw::uiFont(7.0f, true, 0.1f));
        g.setColour(unclaw::colours::muted);
        g.drawText("L+R", area.reduced(6.0f, 4.0f), juce::Justification::topLeft);
        g.setFont(unclaw::monoFont(7.5f));
        g.setColour(peak > 0.0f ? accent : unclaw::colours::muted);
        g.drawText(peak > 1.0e-4f ? juce::String(juce::Decibels::gainToDecibels(peak), 1) + " dB" : juce::String("-inf dB"),
                   area.reduced(6.0f, 4.0f), juce::Justification::bottomRight);
    }

private:
    std::vector<float>& audioBuffer;
    std::array<juce::Path, 4> history;   // [0] — текущий кадр, дальше — послесвечение
};
