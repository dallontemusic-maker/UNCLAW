#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

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
        juce::ColourGradient bg(juce::Colour(0xff17191a), 0.0f, 0.0f,
                                juce::Colour(0xff0b0d0e), area.getWidth(), area.getHeight(), false);
        g.setGradientFill(bg);
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(juce::Colour(0xff333638));
        for (int i = 1; i < 4; ++i) {
            const float x = area.getWidth() * (float) i / 4.0f;
            const float y = area.getHeight() * (float) i / 4.0f;
            g.drawVerticalLine((int) x, 0.0f, area.getHeight());
            g.drawHorizontalLine((int) y, 0.0f, area.getWidth());
        }
        g.setColour(juce::Colour(0xff575a57));
        g.drawHorizontalLine((int) (area.getHeight() * 0.5f), 0.0f, area.getWidth());
        g.setColour(juce::Colour(0xffffc928).withAlpha(0.25f));
        g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);

        if (audioBuffer.empty()) return;

        juce::Path wavePath;
        float width = (float)getWidth();
        float height = (float)getHeight();
        float centerY = height * 0.5f;

        wavePath.startNewSubPath(0, centerY);

        size_t bufferSize = audioBuffer.size();
        for (size_t i = 0; i < bufferSize; ++i) {
            float x = (float)i / (float)bufferSize * width;
            float y = centerY - (audioBuffer[i] * centerY * 0.8f);
            wavePath.lineTo(x, y);
        }

        g.setColour(juce::Colour(0xffffc928).withAlpha(0.16f));
        g.strokePath(wavePath, juce::PathStrokeType(4.0f));
        g.setColour(juce::Colour(0xffffd84d));
        g.strokePath(wavePath, juce::PathStrokeType(1.8f));
    }

private:
    std::vector<float>& audioBuffer;
};
