#include "PluginProcessor.h"
#include "PluginEditor.h"

AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor (WavetableSynthAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), oscilloscopeDisplay (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&yellowStyle);

    setSize (1000, 680);

    // Collapsible Browser Button setup
    toggleBrowserBtn.setClickingTogglesState(true);
    toggleBrowserBtn.setToggleState(true, juce::dontSendNotification);
    toggleBrowserBtn.onClick = [this]() {
        isBrowserOpen = toggleBrowserBtn.getToggleState();
        toggleBrowserBtn.setButtonText(isBrowserOpen ? "< HIDE" : "BROWSER >");
        resized();
        repaint();
    };
    addAndMakeVisible(toggleBrowserBtn);

    // Browser Preset List Box
    addAndMakeVisible(presetListBox);

    // Oscilloscope Component
    addAndMakeVisible(oscilloscopeDisplay);

    // Helper for Rotaries Setup
    auto setupSlider = [this](juce::Slider& slider) {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 55, 15);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffffcc00));
        addAndMakeVisible(slider);
    };

    setupSlider(wtPositionSlider);
    setupSlider(filterCutoffSlider);
    setupSlider(filterResoSlider);
    setupSlider(masterGainSlider);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff111215));

    // Header Branding
    g.setColour(juce::Colour(0xffffcc00));
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("UNCLAW", 20, 12, 100, 24, juce::Justification::left);

    g.setColour(juce::Colour(0xff5f6475));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("// WAVETABLE SYNTHESIZER", 115, 15, 200, 20, juce::Justification::left);

    // Draw Section Container Helper
    auto drawSection = [&g](juce::Rectangle<int> bounds, const juce::String& title) {
        g.setColour(juce::Colour(0xff181a20));
        g.fillRoundedRectangle(bounds.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff090a0c));
        g.drawRoundedRectangle(bounds.toFloat(), 4.0f, 1.5f);

        if (title.isNotEmpty())
        {
            g.setColour(juce::Colour(0xffffcc00));
            g.fillRoundedRectangle(bounds.getX() + 8, bounds.getY() + 5, 16, 2.5f, 1.0f);

            g.setColour(juce::Colour(0xff8a90a0));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(title, bounds.getX() + 30, bounds.getY() + 6, 150, 14, juce::Justification::left);
        }
    };

    int browserWidth = isBrowserOpen ? 210 : 40;

    // Panel 1: Browser (Left)
    drawSection(juce::Rectangle<int>(15, 45, browserWidth, 410), isBrowserOpen ? "PRESET BROWSER" : "");

    // Panel 2: Central Oscilloscope
    int centerLeft = 25 + browserWidth;
    int centerWidth = getWidth() - centerLeft - 225;
    drawSection(juce::Rectangle<int>(centerLeft, 45, centerWidth, 410), "OSCILLOSCOPE & WAVEFORM");

    // Panel 3: Right Controls
    drawSection(juce::Rectangle<int>(getWidth() - 215, 45, 200, 410), "SYNTH CONTROLS");

    // Panel 4: Bottom Piano Keyboard Container
    drawSection(juce::Rectangle<int>(15, 465, getWidth() - 30, 200), "KEYBOARD & MODULATION");
}

void AudioPluginAudioProcessorEditor::resized()
{
    int browserWidth = isBrowserOpen ? 210 : 40;

    // Toggle Button Position
    if (isBrowserOpen)
    {
        toggleBrowserBtn.setBounds(145, 50, 60, 20);
        presetListBox.setVisible(true);
        presetListBox.setBounds(25, 75, 190, 370);
    }
    else
    {
        toggleBrowserBtn.setBounds(20, 50, 30, 400);
        presetListBox.setVisible(false);
    }

    // Central Oscilloscope Dynamic Placement
    int centerLeft = 25 + browserWidth;
    int centerWidth = getWidth() - centerLeft - 225;

    oscilloscopeDisplay.setBounds(centerLeft + 12, 75, centerWidth - 24, 365);

    // Right Panel Controls
    int rightColLeft = getWidth() - 200;
    wtPositionSlider.setBounds(rightColLeft + 15, 75, 75, 75);
    filterCutoffSlider.setBounds(rightColLeft + 110, 75, 75, 75);
    filterResoSlider.setBounds(rightColLeft + 15, 170, 75, 75);
    masterGainSlider.setBounds(rightColLeft + 60, 280, 85, 85);
}
