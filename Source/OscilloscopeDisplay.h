#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class OscilloscopeDisplay : public juce::Component, private juce::Timer
{
public:
    OscilloscopeDisplay(WavetableSynthAudioProcessor& p) : audioProcessor(p)
    {
        startTimerHz(60); // Smooth 60 FPS refresh
    }

    void paint(juce::Graphics& g) override
    {
        auto width = (float)getWidth();
        auto height = (float)getHeight();

        // Dark Glass Panel Base
        g.setColour(juce::Colour(0xff08090b));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
        g.setColour(juce::Colour(0xff222630));
        g.drawRoundedRectangle(getLocalBounds().toFloat(), 3.0f, 1.0f);

        // Grid Lines
        g.setColour(juce::Colour(0xff14171d));
        for (int i = 1; i < 6; ++i)
        {
            g.drawHorizontalLine((int)(height * i / 6.0f), 0.0f, width);
            g.drawVerticalLine((int)(width * i / 6.0f), 0.0f, height);
        }

        // Wavepath Generation
        std::array<float, WavetableSynthAudioProcessor::oscilloscopeSize> samples {};
        audioProcessor.copyOscilloscopeBuffer(samples.data(), (int) samples.size());
        juce::Path wavePath;
        wavePath.startNewSubPath(0, height * 0.5f);

        for (float x = 0; x < width; x += 2.0f)
        {
            const auto index = juce::jlimit(0, (int) samples.size() - 1, (int) (x / juce::jmax(1.0f, width) * (samples.size() - 1)));
            float sample = juce::jlimit(-1.0f, 1.0f, samples[(size_t) index]) * 0.85f;
            float y = (0.5f - sample) * height;
            wavePath.lineTo(x, y);
        }

        // Yellow Semi-transparent Fill Under Waveform
        juce::Path fillPath = wavePath;
        fillPath.lineTo(width, height);
        fillPath.lineTo(0, height);
        fillPath.closeSubPath();

        juce::ColourGradient fillGrad(juce::Colour(0x44ffcc00), 0, 0, juce::Colours::transparentBlack, 0, height, false);
        g.setGradientFill(fillGrad);
        g.fillPath(fillPath);

        // Vivid Amber Yellow Wave Line
        g.setColour(juce::Colour(0xffffcc00));
        g.strokePath(wavePath, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved));
    }

    void resized() override {}

private:
    void timerCallback() override { repaint(); }
    WavetableSynthAudioProcessor& audioProcessor;
};
