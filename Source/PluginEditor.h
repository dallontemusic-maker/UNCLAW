#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "OscilloscopeDisplay.h"

// Custom LookAndFeel in Unclaw Yellow/Black Style
class UnclawYellowStyle : public juce::LookAndFeel_V4
{
public:
    UnclawYellowStyle()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff111215));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff181a20));
        setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffffcc00));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xff111215));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8a90a0));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<float>(x, y, width, height).reduced(3.0f);
        auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centreX = bounds.getCentreX();
        auto centreY = bounds.getCentreY();
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Knob Shadow & Body
        g.setColour(juce::Colour(0xff090a0c));
        g.fillEllipse(centreX - radius, centreY - radius, radius * 2.0f, radius * 2.0f);

        auto innerRadius = radius - 3.0f;
        juce::ColourGradient knobGrad(juce::Colour(0xff2c303a), centreX, centreY - innerRadius,
                                    juce::Colour(0xff16181e), centreX, centreY + innerRadius, false);
        g.setGradientFill(knobGrad);
        g.fillEllipse(centreX - innerRadius, centreY - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);

        // Bright Yellow Active Arc
        juce::Path valueArc;
        valueArc.addCentredArc(centreX, centreY, radius - 1.5f, radius - 1.5f, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(juce::Colour(0xffffcc00));
        g.strokePath(valueArc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Indicator Line
        juce::Path pointer;
        auto pointerLength = innerRadius * 0.6f;
        pointer.addRectangle(-1.25f, -innerRadius + 2.0f, 2.5f, pointerLength);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        g.setColour(juce::Colours::white);
        g.fillPath(pointer);
    }
};

class AudioPluginAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    AudioPluginAudioProcessorEditor (WavetableSynthAudioProcessor&);
    ~AudioPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    WavetableSynthAudioProcessor& audioProcessor;
    UnclawYellowStyle yellowStyle;

    OscilloscopeDisplay oscilloscopeDisplay;

    // Browser Panel Toggle State
    bool isBrowserOpen = true;

    // UI Elements
    juce::TextButton toggleBrowserBtn { "< HIDE" };
    juce::ListBox presetListBox;

    juce::Slider wtPositionSlider;
    juce::Slider filterCutoffSlider;
    juce::Slider filterResoSlider;
    juce::Slider masterGainSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioPluginAudioProcessorEditor)
};
