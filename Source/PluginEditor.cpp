#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

WavetableSynthAudioProcessorEditor::WavetableSynthAudioProcessorEditor(WavetableSynthAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), oscilloscope(p.oscilloscopeBuffer),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {

    addAndMakeVisible(oscilloscope);
    keyboard.setAvailableRange(48, 83);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffe8e4d9));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff17191c));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff55595e));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0xffffc928).withAlpha(0.35f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xffffc928).withAlpha(0.65f));
    addAndMakeVisible(keyboard);

    for (auto* b : { &loadOsc1Button, &loadOsc2Button }) {
        b->addListener(this);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242629));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffbd9418));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe6e4dd));
        addAndMakeVisible(b);
    }

    auto styleCommon = [](juce::Slider& s) {
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd8d5cb));
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff111315));
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff383b3e));
    };

    // Круглая ручка
    auto addKnob = [&](juce::Slider& s, const juce::String& id) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff55585b));
        s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffffc928));
        s.setColour(juce::Slider::thumbColourId, juce::Colour(0xffffd84d));
        styleCommon(s);
        addAndMakeVisible(s);
        sliderAttachments.push_back(std::make_unique<SliderAttachment>(p.parameters, id, s));
    };

    // Выпадающий список: пункты берутся прямо из параметра, поэтому списки не расходятся с процессором
    auto addCombo = [&](juce::ComboBox& box, const juce::String& id) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p.parameters.getParameter(id)))
            box.addItemList(choice->choices, 1);
        box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff141619));
        box.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe5e2d8));
        box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff45494d));
        box.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffffc928));
        addAndMakeVisible(box);
        comboAttachments.push_back(std::make_unique<ComboAttachment>(p.parameters, id, box));
    };

    auto addToggle = [&](juce::ToggleButton& b, const juce::String& id) {
        b.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe3e0d7));
        b.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xffffc928));
        b.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff62666a));
        addAndMakeVisible(b);
        buttonAttachments.push_back(std::make_unique<ButtonAttachment>(p.parameters, id, b));
    };

    for (int n = 0; n < 2; ++n) {
        auto& ui = oscUI[(size_t) n];
        const juce::String pre = "OSC" + juce::String(n + 1) + "_";
        addCombo(ui.wave, pre + "WAVE");
        addKnob(ui.wtPos, "WT_POS_" + juce::String(n + 1));
        addKnob(ui.level, pre + "LEVEL");
        addKnob(ui.oct, pre + "OCT");
        addKnob(ui.semi, pre + "SEMI");
        addKnob(ui.fine, pre + "FINE");
        addKnob(ui.phase, pre + "PHASE");
        addKnob(ui.pan, pre + "PAN");
        addKnob(ui.unison, pre + "UNISON");
        addKnob(ui.detune, pre + "DETUNE");
        addKnob(ui.stereo, pre + "STEREO");
        addKnob(ui.attack, pre + "ENV_ATTACK");
        addKnob(ui.decay, pre + "ENV_DECAY");
        addKnob(ui.sustain, pre + "ENV_SUSTAIN");
        addKnob(ui.release, pre + "ENV_RELEASE");
        addToggle(ui.toFilter, pre + "FILTER");
        ui.analogButton.setClickingTogglesState(true);
        ui.analogButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202326));
        ui.analogButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6f591c));
        ui.analogButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaa9a2));
        ui.analogButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffd34a));
        addAndMakeVisible(ui.analogButton);
        buttonAttachments.push_back(std::make_unique<ButtonAttachment>(p.parameters, pre + "ANALOG", ui.analogButton));
    }
    addCombo(subWaveBox, "SUB_WAVE");
    addKnob(subLevelSlider, "SUB_LEVEL");
    addKnob(subAttackSlider, "SUB_ENV_ATTACK");
    addKnob(subDecaySlider, "SUB_ENV_DECAY");
    addKnob(subSustainSlider, "SUB_ENV_SUSTAIN");
    addKnob(subReleaseSlider, "SUB_ENV_RELEASE");
    addToggle(subToFilterButton, "SUB_FILTER");

    addKnob(cutoffSlider, "CUTOFF");
    addKnob(resonanceSlider, "RESONANCE");
    addToggle(filterOnButton, "FILTER_ON");
    addCombo(filterTypeBox, "FILTER_TYPE");

    addKnob(filterEnvAttackSlider, "FILTER_ENV_ATTACK");
    addKnob(filterEnvDecaySlider, "FILTER_ENV_DECAY");
    addKnob(filterEnvSustainSlider, "FILTER_ENV_SUSTAIN");
    addKnob(filterEnvReleaseSlider, "FILTER_ENV_RELEASE");
    addKnob(filterEnvAmountSlider, "FILTER_ENV_AMOUNT");
    addKnob(masterVolumeSlider, "MASTER_VOLUME");
    addKnob(masterPanSlider, "MASTER_PAN");
    for (int n = 0; n < 2; ++n) {
        auto& lfo = lfoUI[(size_t) n];
        const juce::String pre = "LFO" + juce::String(n + 1) + "_";
        addToggle(lfo.enabled, pre + "ON");
        addCombo(lfo.destination, pre + "DEST");
        addCombo(lfo.waveform, pre + "WAVE");
        addKnob(lfo.rate, pre + "RATE");
        addToggle(lfo.tempoSync, pre + "SYNC");
        addCombo(lfo.syncDivision, pre + "DIV");
        addKnob(lfo.depth, pre + "DEPTH");
    }
    addCombo(voiceModeBox, "VOICE_MODE");
    addCombo(voiceCountBox, "VOICE_COUNT");
    addKnob(glideSlider, "GLIDE_TIME");
    addCombo(pitchRangeBox, "PITCH_RANGE");
    for (auto* wheel : { &pitchWheelSlider, &modWheelSlider }) {
        wheel->setSliderStyle(juce::Slider::LinearVertical);
        wheel->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
        wheel->setColour(juce::Slider::trackColourId, juce::Colour(0xffffc928));
        wheel->setColour(juce::Slider::thumbColourId, juce::Colour(0xffdedbd1));
        wheel->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff34373a));
        wheel->setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd8d5cb));
        wheel->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        wheel->setTextBoxIsEditable(false);
        addAndMakeVisible(wheel);
    }
    pitchWheelSlider.setRange(0, 16383, 1);
    pitchWheelSlider.setValue(8192, juce::dontSendNotification);
    pitchWheelSlider.setDoubleClickReturnValue(true, 8192);
    pitchWheelSlider.onValueChange = [this] { audioProcessor.setUiPitchWheelValue((int) pitchWheelSlider.getValue()); };
    modWheelSlider.setRange(0, 127, 1);
    modWheelSlider.setValue(0, juce::dontSendNotification);
    modWheelSlider.onValueChange = [this] { audioProcessor.setUiModWheelValue((int) modWheelSlider.getValue()); };

    addCombo(revModeBox, "REV_MODE");
    addKnob(revSizeSlider, "REV_SIZE");
    addKnob(revDecaySlider, "REV_DECAY");
    addKnob(revToneSlider, "REV_TONE");
    addKnob(revMixSlider, "REV_MIX");

    addToggle(delSyncButton, "DEL_SYNC");
    addCombo(delNoteBox, "DEL_NOTE");
    addKnob(delToneSlider, "DEL_TONE");
    addKnob(delMixSlider, "DEL_MIX");

    addKnob(satDriveSlider, "SAT_DRIVE");
    addKnob(satMixSlider, "SAT_MIX");

    addToggle(arpEnableButton, "ARP_ON");
    addCombo(arpModeBox, "ARP_MODE");
    addCombo(arpRateBox, "ARP_RATE");
    addKnob(arpOctSlider, "ARP_OCT");
    addKnob(arpGateSlider, "ARP_GATE");

    // ---------------- браузер вейвтейблов ----------------
    scanThread.startThread();

    targetOsc1Button.setRadioGroupId(1001);
    targetOsc2Button.setRadioGroupId(1001);
    for (auto* b : { &targetOsc1Button, &targetOsc2Button }) {
        b->setClickingTogglesState(true);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff17191b));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffbd9418));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaa9a2));
        b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffe27a));
        addAndMakeVisible(b);
    }
    targetOsc1Button.setToggleState(true, juce::dontSendNotification);
    targetOsc1Button.onClick = [this] { targetOsc = 1; };
    targetOsc2Button.onClick = [this] { targetOsc = 2; };

    for (auto* b : { &addFolderButton, &removeFolderButton }) {
        b->addListener(this);
        b->setColour(juce::TextButton::buttonColourId, juce::Colours::black);
        b->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(b);
    }

    folderBox.setTextWhenNothingSelected("No folder added");
    folderBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff141619));
    folderBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xffe5e2d8));
    folderBox.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff45494d));
    folderBox.setColour(juce::ComboBox::arrowColourId, juce::Colour(0xffffc928));
    folderBox.onChange = [this] { showFolder(folderBox.getSelectedItemIndex()); };
    addAndMakeVisible(folderBox);

    folderTree.setColour(juce::TreeView::backgroundColourId, juce::Colour(0xff121416));
    folderTree.setColour(juce::TreeView::linesColourId, juce::Colour(0xff5b5e61));
    folderTree.setColour(juce::DirectoryContentsDisplayComponent::textColourId, juce::Colour(0xffc8c6bf));
    folderTree.setColour(juce::DirectoryContentsDisplayComponent::highlightColourId, juce::Colour(0xffffc928).withAlpha(0.28f));
    folderTree.setColour(juce::DirectoryContentsDisplayComponent::highlightedTextColourId, juce::Colour(0xffffdf68));
    folderTree.setItemHeight(22);
    folderTree.addListener(this);
    addAndMakeVisible(folderTree);

    for (auto* l : { &hintLabel, &status1Label, &status2Label }) {
        l->setColour(juce::Label::textColourId, juce::Colour(0xffaaa9a2));
        l->setFont(juce::FontOptions(11.5f));
        l->setMinimumHorizontalScale(1.0f);   // переносить длинный текст, а не сжимать шрифт
        l->setJustificationType(juce::Justification::topLeft);
        addAndMakeVisible(l);
    }
    hintLabel.setText("Click \"Add Folder...\" and choose a folder with .wav waveforms. "
                      "Click a file to load it into the selected oscillator.", juce::dontSendNotification);

    browserFolders = browserSettings.getFolders();
    refreshFolderBox(juce::jlimit(0, juce::jmax(0, browserFolders.size() - 1), browserSettings.getLastFolderIndex()));
    startTimerHz(20);
    timerCallback();

    setSize(1450, 1220);
}

WavetableSynthAudioProcessorEditor::~WavetableSynthAudioProcessorEditor() {
    stopTimer();
    audioProcessor.setUiPitchWheelValue(8192);
    audioProcessor.setUiModWheelValue(0);
    folderTree.removeListener(this);
    loadOsc1Button.removeListener(this);
    loadOsc2Button.removeListener(this);
    addFolderButton.removeListener(this);
    removeFolderButton.removeListener(this);
    scanThread.stopThread(2000);
}

void WavetableSynthAudioProcessorEditor::refreshFolderBox(int selectIndex) {
    folderBox.clear(juce::dontSendNotification);
    for (int i = 0; i < browserFolders.size(); ++i) {
        const juce::File f(browserFolders[i]);
        folderBox.addItem(f.getFileName().isNotEmpty() ? f.getFileName() : f.getFullPathName(), i + 1);
    }
    hintLabel.setVisible(browserFolders.isEmpty());
    removeFolderButton.setEnabled(!browserFolders.isEmpty());
    if (browserFolders.isEmpty()) {
        showFolder(-1);
    } else {
        folderBox.setSelectedItemIndex(selectIndex, juce::dontSendNotification);
        showFolder(selectIndex);
    }
}

void WavetableSynthAudioProcessorEditor::showFolder(int index) {
    if (juce::isPositiveAndBelow(index, browserFolders.size())) {
        folderContents.setDirectory(juce::File(browserFolders[index]), true, true);
        browserSettings.setLastFolderIndex(index);
    } else {
        folderContents.setDirectory(juce::File(), false, false);
    }
}

void WavetableSynthAudioProcessorEditor::selectionChanged() {
    const auto file = folderTree.getSelectedFile();
    if (file.existsAsFile()) loadFromBrowser(file);
}

void WavetableSynthAudioProcessorEditor::loadFromBrowser(const juce::File& file) {
    audioProcessor.loadWavetableAsync(file, targetOsc);

    // Чтобы загруженную волну было слышно, переключаем источник этого осциллятора на Wavetable
    if (auto* prm = audioProcessor.parameters.getParameter(targetOsc == 1 ? "OSC1_WAVE" : "OSC2_WAVE")) {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost(0.0f);
        prm->endChangeGesture();
    }
}

void WavetableSynthAudioProcessorEditor::timerCallback() {
    status1Label.setText("Osc 1: " + audioProcessor.getWavetableStatus(1), juce::dontSendNotification);
    status2Label.setText("Osc 2: " + audioProcessor.getWavetableStatus(2), juce::dontSendNotification);
    repaint(outputMeterBounds.expanded(4));
}

void WavetableSynthAudioProcessorEditor::placeKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell) {
    captions.push_back({ cell.removeFromTop(14), name, CaptionKind::Knob });
    slider.setBounds(cell.removeFromTop(68));
}

void WavetableSynthAudioProcessorEditor::placeCompactKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell) {
    captions.push_back({ cell.removeFromTop(13), name, CaptionKind::Knob });
    slider.setBounds(cell.withSizeKeepingCentre(56, 56));
}

void WavetableSynthAudioProcessorEditor::placeCombo(juce::ComboBox& box, const juce::String& name, juce::Rectangle<int> cell) {
    captions.push_back({ cell.removeFromTop(14), name, CaptionKind::Knob });
    box.setBounds(cell.removeFromTop(24));
}

void WavetableSynthAudioProcessorEditor::placeHFader(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> row) {
    captions.push_back({ row.removeFromLeft(52), name, CaptionKind::Side });
    slider.setBounds(row);
}

void WavetableSynthAudioProcessorEditor::addSection(const juce::String& name, juce::Rectangle<int> bounds) {
    captions.push_back({ bounds, name, CaptionKind::Section });
}

void WavetableSynthAudioProcessorEditor::paint(juce::Graphics& g) {
    juce::ColourGradient background(juce::Colour(0xff1b1d20), 0.0f, 0.0f,
                                     juce::Colour(0xff0e1012), 0.0f, (float) getHeight(), false);
    g.setGradientFill(background);
    g.fillAll();
    g.setColour(juce::Colour(0xff292c2f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(7.0f), 9.0f, 1.0f);

    for (const auto& panel : panels) {
        juce::ColourGradient panelFill(juce::Colour(0xff292c2f), (float) panel.bounds.getX(), (float) panel.bounds.getY(),
                                       juce::Colour(0xff1b1d20), (float) panel.bounds.getX(), (float) panel.bounds.getBottom(), false);
        g.setGradientFill(panelFill);
        g.fillRoundedRectangle(panel.bounds.toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff55595d).withAlpha(0.75f));
        g.drawRoundedRectangle(panel.bounds.toFloat(), 5.0f, 1.0f);
        g.setColour(juce::Colour(0xffffc928).withAlpha(0.8f));
        g.fillRoundedRectangle((float) panel.bounds.getX() + 1.0f, (float) panel.bounds.getY() + 9.0f, 2.0f,
                               juce::jmax(20.0f, (float) panel.bounds.getHeight() - 18.0f), 1.0f);
    }
    if (!outputMeterBounds.isEmpty()) {
        g.setColour(juce::Colour(0xff111315));
        g.fillRoundedRectangle(outputMeterBounds.toFloat(), 3.0f);
        const int barGap = 7;
        const int barWidth = juce::jmax(8, (outputMeterBounds.getWidth() - barGap * 3) / 2);
        const int segmentCount = 18;
        const int segmentGap = 2;
        const int segmentHeight = juce::jmax(3, (outputMeterBounds.getHeight() - 24 - segmentGap * (segmentCount - 1)) / segmentCount);
        for (int channel = 0; channel < 2; ++channel) {
            const float peak = juce::jlimit(0.0f, 1.0f, audioProcessor.getOutputPeak(channel));
            const float level = peak > 0.0f ? juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(peak) + 48.0f) / 48.0f) : 0.0f;
            const int litCount = (int) std::lround(level * (float) segmentCount);
            const int x = outputMeterBounds.getX() + barGap + channel * (barWidth + barGap);
            for (int segment = 0; segment < segmentCount; ++segment) {
                const int y = outputMeterBounds.getBottom() - 20 - segmentHeight - segment * (segmentHeight + segmentGap);
                const bool lit = segmentCount - segment <= litCount;
                const bool hot = segment >= 15;
                g.setColour(lit ? (hot ? juce::Colour(0xffffb947) : juce::Colour(0xffffc928))
                                : juce::Colour(0xff35383a));
                g.fillRoundedRectangle((float) x, (float) y, (float) barWidth, (float) segmentHeight, 1.2f);
            }
            g.setColour(juce::Colour(0xffbcb9ae));
            g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            g.drawText(channel == 0 ? "L" : "R", x, outputMeterBounds.getBottom() - 16, barWidth, 12, juce::Justification::centred);
        }
        g.setColour(juce::Colour(0xffbcb9ae));
        g.setFont(juce::FontOptions(8.0f));
        g.drawText("OUT", outputMeterBounds.getX(), outputMeterBounds.getY() - 13,
                   outputMeterBounds.getWidth(), 12, juce::Justification::centred);
    }
    // Compact hardware-style header and warm yellow brand accent.
    g.setColour(juce::Colour(0xff151719));
    g.fillRoundedRectangle(18.0f, 10.0f, (float) getWidth() - 36.0f, 48.0f, 5.0f);
    g.setColour(juce::Colour(0xff45494c));
    g.drawRoundedRectangle(18.0f, 10.0f, (float) getWidth() - 36.0f, 48.0f, 5.0f, 1.0f);
    juce::Path mark;
    mark.startNewSubPath(34.0f, 43.0f); mark.lineTo(45.0f, 20.0f); mark.lineTo(55.0f, 43.0f);
    mark.lineTo(48.0f, 39.0f); mark.lineTo(40.0f, 43.0f);
    g.setColour(juce::Colour(0xffffc928));
    g.strokePath(mark, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffe7e3d8));
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText("UNCLAW", 66, 17, 132, 22, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffffc928));
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("WAVETABLE SYNTHESIZER", 67, 37, 170, 12, juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffb1aea5));
    g.setFont(juce::FontOptions(9.5f));
    g.drawText("ANALOG ENGINE  ·  2 OSC  ·  SUB", getWidth() - 310, 25, 265, 15, juce::Justification::centredRight);
    g.setColour(juce::Colour(0xff383b3e));
    g.drawVerticalLine(250, 58.0f, (float) getHeight());

    for (const auto& c : captions) {
        switch (c.kind) {
            case CaptionKind::Section:
                g.setColour(juce::Colour(0xffffd34a));
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                g.drawText(c.text, c.bounds, juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff56595b));
                g.drawHorizontalLine(c.bounds.getBottom() - 1, (float) c.bounds.getX(), (float) c.bounds.getRight());
                break;
            case CaptionKind::Knob:
                g.setColour(juce::Colour(0xffc4c1b8));
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(c.text, c.bounds, juce::Justification::centred);
                break;
            case CaptionKind::Side:
                g.setColour(juce::Colour(0xffc4c1b8));
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText(c.text, c.bounds, juce::Justification::centredLeft);
                break;
        }
    }
}

void WavetableSynthAudioProcessorEditor::resized() {
    captions.clear();
    panels.clear();
    auto bounds = getLocalBounds();

    // ---------------- браузер (слева) ----------------
    auto browser = bounds.removeFromLeft(250).reduced(12, 68);
    addSection("WAVEFORM BROWSER", browser.removeFromTop(20));
    browser.removeFromTop(8);

    auto targetRow = browser.removeFromTop(26);
    captions.push_back({ targetRow.removeFromLeft(72), "Load into:", CaptionKind::Side });
    targetOsc1Button.setBounds(targetRow.removeFromLeft(targetRow.getWidth() / 2).reduced(2, 0));
    targetOsc2Button.setBounds(targetRow.reduced(2, 0));
    browser.removeFromTop(8);

    folderBox.setBounds(browser.removeFromTop(26));
    browser.removeFromTop(6);
    auto folderButtons = browser.removeFromTop(26);
    addFolderButton.setBounds(folderButtons.removeFromLeft(folderButtons.getWidth() * 3 / 5).reduced(2, 0));
    removeFolderButton.setBounds(folderButtons.reduced(2, 0));
    browser.removeFromTop(8);

    auto statusArea = browser.removeFromBottom(64);
    statusArea.removeFromTop(6);
    status1Label.setBounds(statusArea.removeFromTop(28));
    status2Label.setBounds(statusArea.removeFromTop(28));
    folderTree.setBounds(browser);
    hintLabel.setBounds(browser.reduced(6));

    auto area = bounds.reduced(20);
    area.removeFromTop(38);                                   // резерв для аппаратной шапки
    area.removeFromTop(4);

    constexpr int kCompactRow = 70;

    // Three sound sources share one aligned top row: OSC 1, OSC 2 and SUB.
    auto sourceRow = area.removeFromTop(300);
    const int sourceGap = 10;
    const int sourceWidth = (sourceRow.getWidth() - sourceGap * 2) / 3;
    for (int n = 0; n < 2; ++n) {
        if (n > 0) sourceRow.removeFromLeft(sourceGap);
        auto card = sourceRow.removeFromLeft(sourceWidth);
        const int panelX = card.getX(), panelY = card.getY();
        auto& ui = oscUI[(size_t) n];
        auto oscHeader = card.removeFromTop(20);
        ui.analogButton.setBounds(oscHeader.removeFromRight(102).reduced(2, 1));
        addSection("OSCILLATOR " + juce::String(n + 1), oscHeader);
        auto sourceControls = card.removeFromTop(40);
        placeCombo(ui.wave, "Wave", sourceControls.removeFromLeft(142));
        sourceControls.removeFromLeft(8);
        auto& loadBtn = (n == 0) ? loadOsc1Button : loadOsc2Button;
        loadBtn.setBounds(sourceControls.removeFromLeft(118).withHeight(26).withY(sourceControls.getY() + 4));

        auto rowA = card.removeFromTop(kCompactRow);
        const int knobWidth = rowA.getWidth() / 5;
        placeCompactKnob(ui.wtPos, "WT Pos", rowA.removeFromLeft(knobWidth));
        placeCompactKnob(ui.level, "Level", rowA.removeFromLeft(knobWidth));
        placeCompactKnob(ui.oct, "Octave", rowA.removeFromLeft(knobWidth));
        placeCompactKnob(ui.semi, "Interval", rowA.removeFromLeft(knobWidth));
        placeCompactKnob(ui.fine, "Fine", rowA);

        auto rowB = card.removeFromTop(kCompactRow);
        const int knobWidthB = rowB.getWidth() / 5;
        placeCompactKnob(ui.phase, "Phase", rowB.removeFromLeft(knobWidthB));
        placeCompactKnob(ui.pan, "Pan", rowB.removeFromLeft(knobWidthB));
        placeCompactKnob(ui.unison, "Unison", rowB.removeFromLeft(knobWidthB));
        placeCompactKnob(ui.detune, "Detune", rowB.removeFromLeft(knobWidthB));
        placeCompactKnob(ui.stereo, "Stereo", rowB);

        addSection("AMP ENVELOPE", card.removeFromTop(18));
        auto envRow = card.removeFromTop(kCompactRow);
        const int envWidth = (envRow.getWidth() - 112) / 4;
        placeCompactKnob(ui.attack, "Attack", envRow.removeFromLeft(envWidth));
        placeCompactKnob(ui.decay, "Decay", envRow.removeFromLeft(envWidth));
        placeCompactKnob(ui.sustain, "Sustain", envRow.removeFromLeft(envWidth));
        placeCompactKnob(ui.release, "Release", envRow.removeFromLeft(envWidth));
        ui.toFilter.setBounds(envRow.removeFromLeft(112).withHeight(28).withY(envRow.getY() + 20));
        panels.push_back({ { panelX, panelY, sourceWidth, 300 }, juce::Colour(0xff292c2f) });
    }

    sourceRow.removeFromLeft(sourceGap);
    auto subCard = sourceRow.removeFromLeft(sourceWidth);
    const int subX = subCard.getX(), subY = subCard.getY();
    addSection("SUB OSCILLATOR", subCard.removeFromTop(20));
    auto subControls = subCard.removeFromTop(78);
    placeCombo(subWaveBox, "Wave", subControls.removeFromLeft(142));
    subControls.removeFromLeft(8);
    placeCompactKnob(subLevelSlider, "Level", subControls.removeFromLeft(80));
    subToFilterButton.setBounds(subControls.removeFromLeft(112).withHeight(28).withY(subControls.getY() + 24));
    addSection("AMP ENVELOPE", subCard.removeFromTop(18));
    auto subEnvRow = subCard.removeFromTop(kCompactRow);
    const int subEnvWidth = subEnvRow.getWidth() / 4;
    placeCompactKnob(subAttackSlider, "Attack", subEnvRow.removeFromLeft(subEnvWidth));
    placeCompactKnob(subDecaySlider, "Decay", subEnvRow.removeFromLeft(subEnvWidth));
    placeCompactKnob(subSustainSlider, "Sustain", subEnvRow.removeFromLeft(subEnvWidth));
    placeCompactKnob(subReleaseSlider, "Release", subEnvRow);
    captions.push_back({ { subX + 12, subY + 205, sourceWidth - 24, 20 },
        "SUB TUNED ONE OCTAVE BELOW", CaptionKind::Side });
    panels.push_back({ { subX, subY, sourceWidth, 300 }, juce::Colour(0xff292c2f) });
    area.removeFromTop(8);

    constexpr int kKnobRow = 14 + 68;

    // ---------------- Filter with its envelope / centered scope ----------------
    auto titleRow = area.removeFromTop(20);
    auto synthRow = area.removeFromTop(220);
    constexpr int scopeColumnWidth = 300;
    const int sideWidth = (synthRow.getWidth() - scopeColumnWidth - 24) / 2;
    auto filterG = synthRow.removeFromLeft(sideWidth); synthRow.removeFromLeft(12);
    auto scopeG = synthRow.removeFromLeft(scopeColumnWidth);
    synthRow.removeFromLeft(12);
    auto masterG = synthRow;
    const int shapeY = synthRow.getY();

    auto span = [&](juce::Rectangle<int> g) { return juce::Rectangle<int>(g.getX(), titleRow.getY(), g.getWidth(), titleRow.getHeight()); };
    addSection("FILTER", span(filterG));
    addSection("OSCILLOSCOPE", span(scopeG));
    addSection("MASTER", span(masterG));
    panels.push_back({ filterG.withY(shapeY).withHeight(220), juce::Colour(0xff292c2f) });
    panels.push_back({ masterG.withY(shapeY).withHeight(220), juce::Colour(0xff292c2f) });

    auto filterHeader = filterG.removeFromTop(30);
    filterOnButton.setBounds(filterHeader.removeFromLeft(112).reduced(2, 2));
    filterTypeBox.setBounds(filterHeader.reduced(2, 2));
    auto filterControls = filterG.removeFromTop(kKnobRow);
    placeCompactKnob(cutoffSlider, "Cutoff", filterControls.removeFromLeft(filterControls.getWidth() / 2));
    placeCompactKnob(resonanceSlider, "Resonance", filterControls);
    addSection("FILTER ENVELOPE (ADSR)", filterG.removeFromTop(18));
    auto filterEnvKnobs = filterG.removeFromTop(kKnobRow);
    placeCompactKnob(filterEnvAttackSlider, "Attack", filterEnvKnobs.removeFromLeft(filterEnvKnobs.getWidth() / 5));
    placeCompactKnob(filterEnvDecaySlider, "Decay", filterEnvKnobs.removeFromLeft(filterEnvKnobs.getWidth() / 4));
    placeCompactKnob(filterEnvSustainSlider, "Sustain", filterEnvKnobs.removeFromLeft(filterEnvKnobs.getWidth() / 3));
    placeCompactKnob(filterEnvReleaseSlider, "Release", filterEnvKnobs.removeFromLeft(filterEnvKnobs.getWidth() / 2));
    placeCompactKnob(filterEnvAmountSlider, "Amount", filterEnvKnobs);
    oscilloscope.setBounds(scopeG.withSizeKeepingCentre(210, 210));

    auto meterCell = masterG.removeFromRight(82);
    outputMeterBounds = meterCell.reduced(10, 28);
    placeCompactKnob(masterVolumeSlider, "Volume", masterG.removeFromLeft(masterG.getWidth() / 2));
    placeCompactKnob(masterPanSlider, "Pan", masterG);

    area.removeFromTop(8);

    // ---------------- два независимых LFO ----------------
    auto lfoTitleRow = area.removeFromTop(20);
    auto lfoRow = area.removeFromTop(112);
    const int lfoGap = 12;
    const int lfoWidth = (lfoRow.getWidth() - lfoGap) / 2;
    for (int n = 0; n < 2; ++n) {
        if (n > 0) lfoRow.removeFromLeft(lfoGap);
        auto card = lfoRow.removeFromLeft(lfoWidth);
        const int cardX = card.getX(), cardY = card.getY();
        auto& ui = lfoUI[(size_t) n];
        auto header = card.removeFromTop(22);
        ui.enabled.setBounds(header.removeFromRight(58).reduced(2, 1));
        addSection("LFO " + juce::String(n + 1),
                   { cardX, lfoTitleRow.getY(), lfoWidth, lfoTitleRow.getHeight() });
        auto targetRow = card.removeFromTop(34);
        placeCombo(ui.destination, "Target", targetRow.removeFromLeft(220));
        targetRow.removeFromLeft(10);
        placeCombo(ui.waveform, "Shape", targetRow.removeFromLeft(180));
        auto controlRow = card;
        placeCompactKnob(ui.rate, "Rate (Hz)", controlRow.removeFromLeft(94));
        ui.tempoSync.setBounds(controlRow.removeFromLeft(76).withHeight(26).withY(controlRow.getY() + 22));
        placeCombo(ui.syncDivision, "Tempo", controlRow.removeFromLeft(120));
        placeCompactKnob(ui.depth, "Depth", controlRow);
        panels.push_back({ { cardX, cardY, lfoWidth, 112 }, juce::Colour(0xff292c2f) });
    }

    area.removeFromTop(8);

    // ---------------- эффекты ----------------
    auto fxTitleRow = area.removeFromTop(20);
    auto fxRow = area.removeFromTop(kKnobRow);
    auto revGroup = fxRow.removeFromLeft(351);  fxRow.removeFromLeft(20);
    auto delGroup = fxRow.removeFromLeft(268);  fxRow.removeFromLeft(20);
    auto satGroup = fxRow.removeFromLeft(128);

    auto fxSpan = [&](juce::Rectangle<int> g) { return juce::Rectangle<int>(g.getX(), fxTitleRow.getY(), g.getWidth(), fxTitleRow.getHeight()); };
    addSection("REVERB", fxSpan(revGroup));
    addSection("DELAY", fxSpan(delGroup));
    addSection("SATURATION", fxSpan(satGroup));

    placeCombo(revModeBox, "Mode", revGroup.removeFromLeft(85));
    revGroup.removeFromLeft(10);
    placeKnob(revSizeSlider, "Size", revGroup.removeFromLeft(64));
    placeKnob(revDecaySlider, "Decay", revGroup.removeFromLeft(64));
    placeKnob(revToneSlider, "Tone", revGroup.removeFromLeft(64));
    placeKnob(revMixSlider, "Mix", revGroup.removeFromLeft(64));

    auto delCol = delGroup.removeFromLeft(130);
    delSyncButton.setBounds(delCol.removeFromTop(24));
    delCol.removeFromTop(8);
    delNoteBox.setBounds(delCol.removeFromTop(24));
    delGroup.removeFromLeft(10);
    placeKnob(delToneSlider, "Tone", delGroup.removeFromLeft(64));
    placeKnob(delMixSlider, "Mix", delGroup.removeFromLeft(64));

    placeKnob(satDriveSlider, "Drive", satGroup.removeFromLeft(64));
    placeKnob(satMixSlider, "Mix", satGroup.removeFromLeft(64));

    area.removeFromTop(8);

    // ---------------- арпеджиатор ----------------
    auto arpTitleRow = area.removeFromTop(20);
    auto arpRow = area.removeFromTop(kKnobRow);
    auto arpG = arpRow.removeFromLeft(453);
    addSection("ARPEGGIATOR", juce::Rectangle<int>(arpG.getX(), arpTitleRow.getY(), arpG.getWidth(), arpTitleRow.getHeight()));

    auto onCell = arpG.removeFromLeft(90);
    onCell.removeFromTop(14);
    arpEnableButton.setBounds(onCell.removeFromTop(24));
    arpG.removeFromLeft(15);
    placeCombo(arpModeBox, "Mode", arpG.removeFromLeft(95));
    arpG.removeFromLeft(15);
    placeCombo(arpRateBox, "Rate", arpG.removeFromLeft(80));
    arpG.removeFromLeft(15);
    placeKnob(arpOctSlider, "Octaves", arpG.removeFromLeft(64));
    arpG.removeFromLeft(15);
    placeKnob(arpGateSlider, "Gate", arpG.removeFromLeft(64));

    area.removeFromTop(8);
    addSection("PERFORMANCE", area.removeFromTop(20));
    auto performance = area.removeFromTop(122);
    auto wheels = performance.removeFromLeft(110);
    auto wheelTitle = wheels.removeFromTop(14);
    captions.push_back({ wheelTitle, "PITCH / MOD", CaptionKind::Knob });
    const int wheelWidth = 38;
    auto pitchCell = wheels.removeFromLeft(wheelWidth);
    captions.push_back({ pitchCell.removeFromTop(12), "PITCH", CaptionKind::Knob });
    pitchWheelSlider.setBounds(pitchCell.removeFromTop(78).reduced(6, 0));
    auto modCell = wheels.removeFromLeft(wheelWidth);
    captions.push_back({ modCell.removeFromTop(12), "MOD", CaptionKind::Knob });
    modWheelSlider.setBounds(modCell.removeFromTop(78).reduced(6, 0));
    pitchRangeBox.setBounds(wheels.removeFromTop(24).withWidth(104));

    performance.removeFromLeft(8);
    auto keyboardArea = performance.removeFromLeft(performance.getWidth() - 154);
    keyboard.setBounds(keyboardArea.reduced(2, 10));
    performance.removeFromLeft(8);
    auto voice = performance;
    addSection("VOICE", juce::Rectangle<int>(voice.getX(), area.getY() - 20, voice.getWidth(), 20));
    auto voiceChoices = voice.removeFromTop(40);
    placeCombo(voiceModeBox, "Mode", voiceChoices.removeFromLeft(voiceChoices.getWidth() / 2));
    placeCombo(voiceCountBox, "Voices", voiceChoices);
    placeCompactKnob(glideSlider, "Glide", voice.removeFromLeft(70));
}

void WavetableSynthAudioProcessorEditor::buttonClicked(juce::Button* button) {
    if (button == &addFolderButton) {
        folderChooser = std::make_unique<juce::FileChooser>("Select a folder with waveforms", juce::File::getSpecialLocation(juce::File::userMusicDirectory));
        juce::Component::SafePointer<WavetableSynthAudioProcessorEditor> safeThis(this);
        folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [safeThis](const juce::FileChooser& fc) {
                if (safeThis == nullptr) return;
                const auto dir = fc.getResult();
                if (!dir.isDirectory()) return;
                safeThis->browserSettings.addFolder(dir);
                safeThis->browserFolders = safeThis->browserSettings.getFolders();
                safeThis->refreshFolderBox(safeThis->browserFolders.indexOf(dir.getFullPathName()));
            });
        return;
    }
    if (button == &removeFolderButton) {
        const int idx = folderBox.getSelectedItemIndex();
        if (idx >= 0) {
            browserSettings.removeFolder(idx);
            browserFolders = browserSettings.getFolders();
            refreshFolderBox(juce::jlimit(0, juce::jmax(0, browserFolders.size() - 1), idx));
        }
        return;
    }

    const int targetOsc = (button == &loadOsc1Button) ? 1 : 2;
    chooser = std::make_unique<juce::FileChooser>("Select a WAV file for Wavetable...", juce::File{}, "*.wav;*.aif;*.aiff");

    // SafePointer: если окно плагина закроют, пока открыт диалог, колбэк не обратится к удалённому объекту
    juce::Component::SafePointer<WavetableSynthAudioProcessorEditor> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, targetOsc](const juce::FileChooser& fc) {
            if (safeThis == nullptr) return;
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                safeThis->audioProcessor.loadWavetableAsync(file, targetOsc);
                if (auto* prm = safeThis->audioProcessor.parameters.getParameter(targetOsc == 1 ? "OSC1_WAVE" : "OSC2_WAVE")) {
                    prm->beginChangeGesture(); prm->setValueNotifyingHost(0.0f); prm->endChangeGesture();
                }
            }
        });
}
