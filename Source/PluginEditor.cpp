#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace {
// Строки с «·» и «±» должны собираться из UTF-8, иначе JUCE читает их как Latin-1 и показывает «Â·»
juce::String u8(const char* text) { return juce::String::fromUTF8(text); }
}

WavetableSynthAudioProcessorEditor::WavetableSynthAudioProcessorEditor(WavetableSynthAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p), oscilloscope(p.oscilloscopeBuffer),
      keyboard(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(oscilloscope);
    keyboard.setAvailableRange(48, 83);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffe8e4d9));
    keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff17191c));
    keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff55595e));
    keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, juce::Colour(0xffffc928).withAlpha(0.35f));
    keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0xffffc928).withAlpha(0.65f));
    addAndMakeVisible(keyboard);

    optionsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242629));
    optionsButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff684e2a));
    optionsButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe6e4dd));
    optionsButton.onClick = [this] {
        juce::PopupMenu sizes;
        sizes.addItem(1, u8("Compact  ·  80%"), true, getWidth() < 850);
        sizes.addItem(2, u8("Standard  ·  100%"), true, getWidth() >= 850 && getWidth() < 1020);
        sizes.addItem(3, u8("Large  ·  120%"), true, getWidth() >= 1020);
        juce::PopupMenu menu;
        menu.addSubMenu("Synthesizer size", sizes);
        juce::Component::SafePointer<WavetableSynthAudioProcessorEditor> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&optionsButton), [safeThis](int result) {
            if (safeThis == nullptr || result < 1 || result > 3)
                return;
            const float scales[] { 0.8f, 1.0f, 1.2f };
            safeThis->setSize(juce::roundToInt(728.0f * scales[result - 1]),
                              juce::roundToInt(750.0f * scales[result - 1]));
        });
    };
    addAndMakeVisible(optionsButton);

    for (auto* b : { &loadOsc1Button, &loadOsc2Button }) {
        b->addListener(this);
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242629));
        b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffbd9418));
        b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe6e4dd));
        addAndMakeVisible(b);
    }

    for (auto* tab : { &synthTabButton, &effectsTabButton }) {
        tab->setClickingTogglesState(false);
        tab->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2023));
        tab->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff684e2a));
        tab->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaa9a2));
        tab->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffdc86));
        addAndMakeVisible(tab);
    }
    synthTabButton.onClick = [this] { showPage(0); };
    effectsTabButton.onClick = [this] { showPage(1); };

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
        addToggle(ui.enabled, pre + "ON");
        addToggle(ui.toFilter, pre + "FILTER");
        ui.analogButton.setClickingTogglesState(true);
        ui.analogButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202326));
        ui.analogButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff6f591c));
        ui.analogButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaa9a2));
        ui.analogButton.setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffd34a));
        addAndMakeVisible(ui.analogButton);
        buttonAttachments.push_back(std::make_unique<ButtonAttachment>(p.parameters, pre + "ANALOG", ui.analogButton));
    }
    // ---------------- гибридный источник ----------------
    auto addChoiceButtons = [&](const char* id, const juce::StringArray& labels) {
        auto buttons = std::make_unique<ChoiceButtons>(*p.parameters.getParameter(id), labels);
        addAndMakeVisible(*buttons);
        return buttons;
    };
    hybridModeButtons = addChoiceButtons("SUB_MODE", { "SUB", "NOISE", "SAMPLE" });
    subShapeButtons = addChoiceButtons("SUB_SHAPE", { "SINE", "SQUARE" });
    noiseColourButtons = addChoiceButtons("NOISE_COLOR", { "WHITE", "PINK", "BROWN" });
    hybridRouteButtons = addChoiceButtons("SUB_ROUTE", { "FILTER", "DIRECT", "DRY" });
    // смена режима меняет набор ручек в карточке
    hybridModeButtons->onSelectionChanged = [this] (int) { resized(); repaint(); };
    for (auto* buttons : { subShapeButtons.get(), noiseColourButtons.get() })
        buttons->onSelectionChanged = [this] (int) { repaint(hybridPreviewBounds); };

    addKnob(subLevelSlider, "SUB_LEVEL");
    addKnob(subOctaveSlider, "SUB_OCTAVE");
    addKnob(hybridFineSlider, "SUB_FINE");
    addKnob(hybridPanSlider, "SUB_PAN");
    addKnob(noiseTiltSlider, "NOISE_TILT");
    addKnob(noiseStereoSlider, "NOISE_STEREO");
    addKnob(samplePitchSlider, "SMP_PITCH");
    addKnob(sampleStartSlider, "SMP_START");
    addToggle(subEnabledButton, "SUB_ON");
    addKnob(subAttackSlider, "SUB_ENV_ATTACK");
    addKnob(subDecaySlider, "SUB_ENV_DECAY");
    addKnob(subSustainSlider, "SUB_ENV_SUSTAIN");
    addKnob(subReleaseSlider, "SUB_ENV_RELEASE");

    // экран сэмпла добавляется раньше своих переключателей, чтобы они лежали поверх него
    sampleDisplay.onClick = [this] {
        sampleChooser = std::make_unique<juce::FileChooser>("Select a sample...", audioProcessor.getHybridFile(),
                                                            "*.wav;*.aif;*.aiff;*.flac");
        juce::Component::SafePointer<WavetableSynthAudioProcessorEditor> safeThis(this);
        sampleChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis](const juce::FileChooser& fc) {
                if (safeThis != nullptr && fc.getResult().existsAsFile())
                    safeThis->loadHybridSample(fc.getResult());
            });
    };
    addAndMakeVisible(sampleDisplay);
    for (auto* b : { &sampleLoopButton, &sampleReverseButton, &sampleKeytrackButton })
        b->setTooltip(b == &sampleLoopButton ? "Loop from the start point to the end"
                      : b == &sampleReverseButton ? "Play the sample backwards" : "Follow the keyboard (off: always root pitch)");
    addToggle(sampleLoopButton, "SMP_LOOP");
    addToggle(sampleReverseButton, "SMP_REVERSE");
    addToggle(sampleKeytrackButton, "SMP_KEYTRACK");
    for (int note = 0; note < 128; ++note)
        sampleRootBox.addItem(hybrid::noteName(note), note + 1);
    sampleRootBox.setTooltip("Root note: the key that plays the sample at its original pitch");
    addCombo(sampleRootBox, "SMP_ROOT");

    addKnob(cutoffSlider, "CUTOFF");
    addKnob(resonanceSlider, "RESONANCE");
    addToggle(filterOnButton, "FILTER_ON");
    filterTypeButtons = std::make_unique<ChoiceButtons>(*p.parameters.getParameter("FILTER_TYPE"), juce::StringArray { "LP", "BP", "HP" });
    addAndMakeVisible(*filterTypeButtons);

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
    addToggle(revOnButton, "REV_ON");
    addKnob(revSizeSlider, "REV_SIZE");
    addKnob(revDecaySlider, "REV_DECAY");
    addKnob(revToneSlider, "REV_TONE");
    addKnob(revMixSlider, "REV_MIX");

    addToggle(delSyncButton, "DEL_SYNC");
    addToggle(delOnButton, "DEL_ON");
    addCombo(delNoteBox, "DEL_NOTE");
    addKnob(delToneSlider, "DEL_TONE");
    addKnob(delMixSlider, "DEL_MIX");

    addKnob(satDriveSlider, "SAT_DRIVE");
    addToggle(satOnButton, "SAT_ON");
    addKnob(satMixSlider, "SAT_MIX");

    addToggle(arpEnableButton, "ARP_ON");
    addCombo(arpModeBox, "ARP_MODE");
    addCombo(arpRateBox, "ARP_RATE");
    addKnob(arpOctSlider, "ARP_OCT");
    addKnob(arpGateSlider, "ARP_GATE");

    // ---------------- браузер вейвтейблов ----------------
    scanThread.startThread();

    for (auto* b : { &targetOsc1Button, &targetOsc2Button })
        b->setTooltip("Click a file in the browser to load it into this oscillator");
    targetOsc1Button.setRadioGroupId(1001);
    targetOsc2Button.setRadioGroupId(1001);
    targetSampleButton.setRadioGroupId(1001);
    targetSampleButton.setTooltip("Load into the hybrid source (sampler)");
    for (auto* b : { &targetOsc1Button, &targetOsc2Button, &targetSampleButton }) {
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
    targetSampleButton.onClick = [this] { targetOsc = 3; };

    browserToggleButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff202326));
    browserToggleButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd8d5cb));
    browserToggleButton.onClick = [this] {
        browserVisible = !browserVisible;
        browserToggleButton.setButtonText(browserVisible ? "HIDE" : "SHOW");
        resized();
    };
    addAndMakeVisible(browserToggleButton);

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
    folderTree.addMouseListener(this, true);
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

    // ---------------- цвета источников и двуполярные ручки ----------------
    for (int n = 0; n < 2; ++n) {
        auto& ui = oscUI[(size_t) n];
        unclaw::setAccent({ &ui.wave, &ui.wtPos, &ui.level, &ui.oct, &ui.semi, &ui.fine, &ui.phase, &ui.pan, &ui.unison,
                            &ui.detune, &ui.stereo, &ui.attack, &ui.decay, &ui.sustain, &ui.release, &ui.analogButton,
                            &ui.enabled, &ui.toFilter, n == 0 ? &loadOsc1Button : &loadOsc2Button,
                            n == 0 ? &targetOsc1Button : &targetOsc2Button },
                          n == 0 ? unclaw::colours::green : unclaw::colours::amber);
        for (auto* knob : { &ui.oct, &ui.semi, &ui.fine, &ui.pan })
            knob->getProperties().set(unclaw::bipolarProperty, true);
    }
    unclaw::setAccent({ hybridModeButtons.get(), subShapeButtons.get(), noiseColourButtons.get(), hybridRouteButtons.get(),
                        &subLevelSlider, &subOctaveSlider, &hybridFineSlider, &hybridPanSlider, &noiseTiltSlider,
                        &noiseStereoSlider, &samplePitchSlider, &sampleStartSlider, &subAttackSlider, &subDecaySlider,
                        &subSustainSlider, &subReleaseSlider, &subEnabledButton, &sampleLoopButton, &sampleReverseButton,
                        &sampleKeytrackButton, &sampleRootBox, &sampleDisplay, &targetSampleButton },
                      unclaw::colours::cyan);
    for (auto* knob : { &hybridFineSlider, &hybridPanSlider, &noiseTiltSlider, &samplePitchSlider,
                        &filterEnvAmountSlider, &masterPanSlider })
        knob->getProperties().set(unclaw::bipolarProperty, true);

    browserFolders = browserSettings.getFolders();
    refreshFolderBox(juce::jlimit(0, juce::jmax(0, browserFolders.size() - 1), browserSettings.getLastFolderIndex()));
    startTimerHz(20);
    timerCallback();

    setSize(728, 750);
    setResizable(true, false);
    setResizeLimits(582, 600, 1092, 1125);
    showPage(0);
}

WavetableSynthAudioProcessorEditor::~WavetableSynthAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
    audioProcessor.setUiPitchWheelValue(8192);
    audioProcessor.setUiModWheelValue(0);
    folderTree.removeListener(this);
    folderTree.removeMouseListener(this);
    loadOsc1Button.removeListener(this);
    loadOsc2Button.removeListener(this);
    addFolderButton.removeListener(this);
    removeFolderButton.removeListener(this);
    scanThread.stopThread(2000);
}

void WavetableSynthAudioProcessorEditor::showPage(int page) {
    currentPage = juce::jlimit(0, 1, page);
    synthTabButton.setToggleState(currentPage == 0, juce::dontSendNotification);
    effectsTabButton.setToggleState(currentPage == 1, juce::dontSendNotification);

    const bool showSynth = currentPage == 0;
    const bool showEffects = currentPage == 1;
    auto setVisible = [] (juce::Component& component, bool visible) { component.setVisible(visible); };
    auto setComponentsVisible = [&] (bool visible, auto&... components) {
        (setVisible(components, visible), ...);
    };

    for (auto& osc : oscUI) {
        setComponentsVisible(showSynth, osc.wave, osc.wtPos, osc.level, osc.oct, osc.semi, osc.fine,
                             osc.phase, osc.pan, osc.unison, osc.detune, osc.stereo, osc.attack,
                             osc.decay, osc.sustain, osc.release, osc.analogButton, osc.enabled, osc.toFilter);
    }
    // видимость ручек конкретного режима гибридного источника уточняет layoutHybrid()
    setComponentsVisible(showSynth, *hybridModeButtons, *subShapeButtons, *noiseColourButtons, *hybridRouteButtons,
                         subOctaveSlider, hybridFineSlider, hybridPanSlider, noiseTiltSlider, noiseStereoSlider,
                         samplePitchSlider, sampleStartSlider, sampleLoopButton, sampleReverseButton,
                         sampleKeytrackButton, sampleRootBox, sampleDisplay);
    setComponentsVisible(showSynth, loadOsc1Button, loadOsc2Button, oscilloscope, keyboard,
                         subLevelSlider, subAttackSlider, subDecaySlider, subSustainSlider,
                         subReleaseSlider, subEnabledButton, cutoffSlider, resonanceSlider,
                         filterOnButton, *filterTypeButtons, filterEnvAttackSlider, filterEnvDecaySlider,
                         filterEnvSustainSlider, filterEnvReleaseSlider, filterEnvAmountSlider,
                         masterVolumeSlider, masterPanSlider, pitchWheelSlider, modWheelSlider,
                         pitchRangeBox, voiceModeBox, voiceCountBox, glideSlider);
    for (auto& lfo : lfoUI)
        setComponentsVisible(showSynth, lfo.enabled, lfo.tempoSync, lfo.destination,
                             lfo.waveform, lfo.syncDivision, lfo.rate, lfo.depth);
    setComponentsVisible(showEffects, revModeBox, revSizeSlider, revDecaySlider, revToneSlider,
                         revMixSlider, revOnButton, delSyncButton, delNoteBox, delToneSlider, delMixSlider,
                         delOnButton, satDriveSlider, satMixSlider, satOnButton);
    setComponentsVisible(showSynth, arpEnableButton, arpModeBox, arpRateBox, arpOctSlider, arpGateSlider);
    resized();
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

bool WavetableSynthAudioProcessorEditor::isInterestedInDragSource(const SourceDetails& details) {
    if (currentPage != 0 || details.sourceComponent.get() != &folderTree)
        return false;
    const juce::File file(details.description.toString());
    const auto ext = file.getFileExtension().toLowerCase();
    return file.existsAsFile() && (ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac");
}

void WavetableSynthAudioProcessorEditor::itemDragEnter(const SourceDetails& details) {
    itemDragMove(details);
}

void WavetableSynthAudioProcessorEditor::itemDragMove(const SourceDetails& details) {
    setDragHover(dropTargetAt(details.localPosition));
}

void WavetableSynthAudioProcessorEditor::itemDragExit(const SourceDetails&) {
    setDragHover(0);
}

void WavetableSynthAudioProcessorEditor::itemDropped(const SourceDetails& details) {
    if (const int target = dropTargetAt(details.localPosition))
        loadIntoTarget(juce::File(details.description.toString()), target);
    setDragHover(0);
}

bool WavetableSynthAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    if (currentPage != 0) return false;
    for (const auto& path : files)
        if (WavetableSynthAudioProcessor::isSupportedAudioFile(juce::File(path))) return true;
    return false;
}

void WavetableSynthAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int x, int y) {
    fileDragMove(files, x, y);
}

void WavetableSynthAudioProcessorEditor::fileDragMove(const juce::StringArray&, int x, int y) {
    setDragHover(dropTargetAt({ x, y }));
}

void WavetableSynthAudioProcessorEditor::fileDragExit(const juce::StringArray&) {
    setDragHover(0);
}

void WavetableSynthAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    const int target = dropTargetAt({ x, y });
    setDragHover(0);
    if (target == 0) return;
    for (const auto& path : files) {
        const juce::File file(path);
        if (WavetableSynthAudioProcessor::isSupportedAudioFile(file)) {
            loadIntoTarget(file, target);
            return;
        }
    }
}

int WavetableSynthAudioProcessorEditor::dropTargetAt(juce::Point<int> point) const {
    for (int i = 0; i < (int) oscillatorDropBounds.size(); ++i)
        if (oscillatorDropBounds[(size_t) i].contains(point)) return i + 1;
    return 0;
}

void WavetableSynthAudioProcessorEditor::setDragHover(int target) {
    if (target == dragHoverOscillator) return;
    dragHoverOscillator = target;
    sampleDisplay.setDragHover(target == 3);
    repaint();
}

void WavetableSynthAudioProcessorEditor::loadIntoTarget(const juce::File& file, int target) {
    if (target == 3) loadHybridSample(file);
    else if (target == 1 || target == 2) loadExternalWavetable(file, target);
}

void WavetableSynthAudioProcessorEditor::loadHybridSample(const juce::File& file) {
    juce::Component::SafePointer<WavetableSynthAudioProcessorEditor> safeThis(this);
    audioProcessor.loadHybridSampleAsync(file, [safeThis](bool loaded) {
        if (safeThis == nullptr || !loaded) return;
        // как и с вейвтейблами, режим переключается только после успешной загрузки
        if (auto* parameter = safeThis->audioProcessor.parameters.getParameter("SUB_MODE")) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(2.0f));
            parameter->endChangeGesture();
        }
        safeThis->updateSampleDisplay();
    });
}

void WavetableSynthAudioProcessorEditor::mouseDown(const juce::MouseEvent&) {
    dragStartedFromBrowser = false;
}

void WavetableSynthAudioProcessorEditor::mouseDrag(const juce::MouseEvent& event) {
    if (dragStartedFromBrowser || event.getDistanceFromDragStart() < 5)
        return;
    const auto file = folderTree.getSelectedFile();
    const auto ext = file.getFileExtension().toLowerCase();
    if (!file.existsAsFile() || (ext != ".wav" && ext != ".aif" && ext != ".aiff" && ext != ".flac"))
        return;
    dragStartedFromBrowser = true;
    startDragging(file.getFullPathName(), &folderTree, juce::ScaledImage(), false, nullptr, &event.source);
}

void WavetableSynthAudioProcessorEditor::mouseUp(const juce::MouseEvent&) {
    dragStartedFromBrowser = false;
}

void WavetableSynthAudioProcessorEditor::loadFromBrowser(const juce::File& file) {
    loadIntoTarget(file, targetOsc);
}

void WavetableSynthAudioProcessorEditor::loadExternalWavetable(const juce::File& file, int oscillator) {
    juce::Component::SafePointer<WavetableSynthAudioProcessorEditor> safeThis(this);
    audioProcessor.loadWavetableAsync(file, oscillator, [safeThis, oscillator](bool loaded) {
        if (safeThis == nullptr || !loaded) return;
        // Переключаемся на Wavetable только после успешной загрузки. Если формат не подошёл,
        // пользователь продолжает слышать выбранный ранее источник.
        if (auto* parameter = safeThis->audioProcessor.parameters.getParameter(
                oscillator == 1 ? "OSC1_WAVE" : "OSC2_WAVE")) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(0.0f);
            parameter->endChangeGesture();
        }
        safeThis->repaint(safeThis->wavePreviewBounds[(size_t) oscillator - 1]);
    });
}

void WavetableSynthAudioProcessorEditor::timerCallback() {
    status1Label.setText("Osc 1: " + audioProcessor.getWavetableStatus(1), juce::dontSendNotification);
    status2Label.setText("Osc 2: " + audioProcessor.getWavetableStatus(2), juce::dontSendNotification);
    for (auto& osc : oscUI)
        osc.enabled.setButtonText(osc.enabled.getToggleState() ? "ON" : "OFF");
    subEnabledButton.setButtonText(subEnabledButton.getToggleState() ? "ON" : "OFF");
    if (currentPage == 0) {
        updateSampleDisplay();
        if (!hybridPreviewBounds.isEmpty()) repaint(hybridPreviewBounds);   // Tilt и октава меняются и из хоста
    }
    revOnButton.setButtonText(revOnButton.getToggleState() ? "ON" : "OFF");
    delOnButton.setButtonText(delOnButton.getToggleState() ? "ON" : "OFF");
    satOnButton.setButtonText(satOnButton.getToggleState() ? "ON" : "OFF");
    for (int channel = 0; channel < 2; ++channel) {   // удержание пика: 1 с, затем плавный спад
        const float peak = audioProcessor.getOutputPeak(channel);
        const float level = peak > 0.0f ? juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(peak) + 48.0f) / 48.0f) : 0.0f;
        auto& hold = meterHold[(size_t) channel];
        auto& ticks = meterHoldTicks[(size_t) channel];
        if (level >= hold) { hold = level; ticks = 20; }
        else if (ticks > 0) --ticks;
        else hold = juce::jmax(level, hold - 0.03f);
    }
    for (auto& lfo : lfoUI) {   // при SYNC работает Div, без него — Rate
        const bool sync = lfo.tempoSync.getToggleState();
        lfo.rate.setAlpha(sync ? 0.35f : 1.0f);
        lfo.syncDivision.setAlpha(sync ? 1.0f : 0.45f);
    }
    repaint(outputMeterBounds.expanded(30, 16));
}

void WavetableSynthAudioProcessorEditor::placeKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell) {
    captions.push_back({ cell.removeFromTop(14), name, CaptionKind::Knob });
    slider.setBounds(cell.removeFromTop(68));
}

void WavetableSynthAudioProcessorEditor::placeCompactKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell) {
    const int textHeight = juce::jlimit(8, 13, cell.getHeight() / 5);
    captions.push_back({ cell.removeFromTop(textHeight), name, CaptionKind::Knob });
    const int boxWidth = juce::jlimit(28, 56, cell.getWidth() - 2);
    const int boxHeight = juce::jlimit(9, 14, cell.getHeight() / 5);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, boxWidth, boxHeight);
    slider.setBounds(cell.reduced(1, 0));
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
    using namespace unclaw;
    const float sx = (float) getWidth() / 728.0f;
    const float sy = (float) getHeight() / 750.0f;
    const auto X = [sx](float value) { return value * sx; };
    const auto Y = [sy](float value) { return value * sy; };

    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff141517), 0.0f, 0.0f,
                                           juce::Colour(0xff090a0b), 0.0f, (float) getHeight(), false));
    g.fillAll();
    g.setColour(juce::Colour(0xff1d1f21));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(3.5f), 10.0f, 1.0f);

    // браузер слева — тёмная ниша
    const float rail = browserVisible ? X(152) : X(40);
    const auto railArea = juce::Rectangle<float>(X(6), Y(53), rail - X(6), (float) getHeight() - Y(59));
    g.setColour(juce::Colour(0xff0c0d0e));
    g.fillRoundedRectangle(railArea, 6.0f);
    g.setColour(juce::Colour(0xff1f2123));
    g.drawRoundedRectangle(railArea, 6.0f, 1.0f);

    for (const auto& panel : panels) {
        const auto r = panel.bounds.toFloat().reduced(0.5f);
        g.setGradientFill(juce::ColourGradient(colours::panelTop, r.getX(), r.getY(), colours::panelBottom, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(colours::outline);
        g.drawRoundedRectangle(r, 6.0f, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.07f));   // блик по верхней кромке
        g.drawLine(r.getX() + 6.0f, r.getY() + 0.7f, r.getRight() - 6.0f, r.getY() + 0.7f, 1.0f);
    }
    if (dragHoverOscillator > 0 && dragHoverOscillator <= (int) oscillatorDropBounds.size()) {
        static const juce::Colour dropColours[] { colours::green, colours::amber, colours::cyan };
        const auto colour = dropColours[dragHoverOscillator - 1];
        const auto target = oscillatorDropBounds[(size_t) dragHoverOscillator - 1].toFloat().reduced(1.5f);
        g.setColour(colour.withAlpha(0.06f));
        g.fillRoundedRectangle(target, 6.0f);
        g.setColour(colour);
        g.drawRoundedRectangle(target, 6.0f, 1.5f);
    }

    if (!filterEnvelopeGraphBounds.isEmpty()) {
        const auto graph = filterEnvelopeGraphBounds.toFloat();
        drawScreen(g, graph, 4, 2);
        const juce::Point<float> points[] {
            { graph.getX() + 3.0f, graph.getBottom() - 3.0f },
            { graph.getX() + graph.getWidth() * 0.18f, graph.getY() + 4.0f },
            { graph.getX() + graph.getWidth() * 0.40f, graph.getY() + graph.getHeight() * 0.50f },
            { graph.getX() + graph.getWidth() * 0.72f, graph.getY() + graph.getHeight() * 0.66f },
            { graph.getRight() - 3.0f, graph.getBottom() - 3.0f } };
        juce::Path envelope;
        envelope.startNewSubPath(points[0]);
        for (int i = 1; i < 5; ++i) envelope.lineTo(points[i]);
        auto fill = envelope;
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(colours::amber.withAlpha(0.22f), 0.0f, graph.getY(),
                                               colours::amber.withAlpha(0.0f), 0.0f, graph.getBottom(), false));
        g.fillPath(fill);
        strokeGlow(g, envelope, colours::amber, 1.3f);
        for (int i = 1; i < 4; ++i) {
            const auto dot = juce::Rectangle<float>(4.0f, 4.0f).withCentre(points[i]);
            g.setColour(juce::Colour(0xff0a0b0c));
            g.fillEllipse(dot);
            g.setColour(colours::amber);
            g.drawEllipse(dot, 1.0f);
        }
    }
    for (size_t osc = 0; osc < wavePreviewBounds.size(); ++osc) {
        const auto area = wavePreviewBounds[osc].toFloat();
        if (area.isEmpty()) continue;
        drawScreen(g, area, 6, 2, 3.0f);
        const auto samples = audioProcessor.getWavetablePreview((int) osc + 1);
        juce::Path wave;
        for (size_t i = 0; i < samples.size(); ++i) {
            const float x = area.getX() + 3.0f + (area.getWidth() - 6.0f) * (float) i / (float) (samples.size() - 1);
            const float y = area.getCentreY() - samples[i] * (area.getHeight() * 0.38f);
            if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
        }
        strokeGlow(g, wave, osc == 0 ? colours::green : colours::amber, 1.1f, { { 3.4f, 0.12f }, { 1.9f, 0.25f } });
    }
    paintHybridPreview(g);

    if (!outputMeterBounds.isEmpty()) {
        const auto m = outputMeterBounds;
        g.setColour(juce::Colour(0xff060707));
        g.fillRoundedRectangle(m.toFloat(), 4.0f);
        g.setColour(juce::Colour(0xff1d1f22));
        g.drawRoundedRectangle(m.toFloat(), 4.0f, 1.0f);
        const int barGap = 7;
        const int barWidth = juce::jmax(8, (m.getWidth() - barGap * 3) / 2);
        const int segmentCount = 18;
        const int segmentGap = 2;
        const int segmentHeight = juce::jmax(3, (m.getHeight() - 24 - segmentGap * (segmentCount - 1)) / segmentCount);
        auto segmentY = [&](int segment) { return m.getBottom() - 20 - segmentHeight - segment * (segmentHeight + segmentGap); };
        for (int channel = 0; channel < 2; ++channel) {
            const float peak = juce::jlimit(0.0f, 1.0f, audioProcessor.getOutputPeak(channel));
            const float level = peak > 0.0f ? juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(peak) + 48.0f) / 48.0f) : 0.0f;
            const int litCount = (int) std::lround(level * (float) segmentCount);
            const int holdSegment = (int) std::lround(meterHold[(size_t) channel] * (float) segmentCount) - 1;
            const int x = m.getX() + barGap + channel * (barWidth + barGap);
            for (int segment = 0; segment < segmentCount; ++segment) {
                const auto seg = juce::Rectangle<float>((float) x, (float) segmentY(segment), (float) barWidth, (float) segmentHeight);
                const auto colour = segment >= 15 ? colours::hot : colours::amber;
                if (segment < litCount || (segment == holdSegment && holdSegment > 0)) {
                    g.setColour(colour.withAlpha(0.18f));
                    g.fillRoundedRectangle(seg.expanded(1.0f), 1.6f);
                    g.setColour(colour);
                } else {
                    g.setColour(colour.withAlpha(0.12f));   // погасший сегмент виден, шкала читается и в тишине
                }
                g.fillRoundedRectangle(seg, 1.0f);
            }
            g.setColour(colours::muted);
            g.setFont(uiFont(7.0f));
            g.drawText(channel == 0 ? "L" : "R", x, m.getBottom() - 16, barWidth, 12, juce::Justification::centred);
        }
        g.setFont(uiFont(7.0f, true, 0.1f));
        g.drawText("OUT", m.getX(), m.getY() - 13, m.getWidth(), 12, juce::Justification::centred);
        g.setColour(juce::Colour(0xff5f5d58));
        g.setFont(monoFont(6.0f));
        for (auto [segment, label] : { std::pair<int, const char*> { 17, "0" }, { 14, "-6" }, { 11, "-12" }, { 5, "-30" } })
            g.drawText(label, m.getX() - 22, segmentY(segment) - 3, 19, segmentHeight + 6, juce::Justification::centredRight);
    }

    // шапка
    const auto header = juce::Rectangle<float>(X(6), Y(5), (float) getWidth() - X(12), Y(43));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff16171a), 0.0f, header.getY(), juce::Colour(0xff0e0f10), 0.0f, header.getBottom(), false));
    g.fillRoundedRectangle(header, 6.0f);
    g.setColour(juce::Colour(0xff26282b));
    g.drawRoundedRectangle(header, 6.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawLine(header.getX() + 6.0f, header.getY() + 1.2f, header.getRight() - 6.0f, header.getY() + 1.2f, 1.0f);
    juce::Path mark;
    mark.startNewSubPath(X(77), Y(32)); mark.lineTo(X(87), Y(11)); mark.lineTo(X(97), Y(32));
    mark.lineTo(X(91), Y(29)); mark.lineTo(X(83), Y(32));
    strokeGlow(g, mark, colours::amber, 1.6f, { { 3.2f, 0.12f } });
    g.setColour(colours::text);
    g.setFont(uiFont(Y(15.0f), true, 0.28f));
    g.drawText("UNCLAW", (int) X(103), (int) Y(12), (int) X(120), (int) Y(18), juce::Justification::centredLeft);
    g.setColour(colours::amber);
    g.setFont(uiFont(Y(6.4f), true, 0.32f));
    g.drawText("WAVETABLE SYNTHESIZER", (int) X(104), (int) Y(30), (int) X(160), (int) Y(10), juce::Justification::centredLeft);
    for (float cx : { header.getRight() - X(22), header.getRight() - X(10) }) {   // винты справа
        const auto screw = juce::Rectangle<float>(X(6.8f), X(6.8f)).withCentre({ cx, header.getCentreY() });
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4d51), screw.getX() + screw.getWidth() * 0.35f, screw.getY() + screw.getHeight() * 0.3f,
                                               juce::Colour(0xff16171a), screw.getRight(), screw.getBottom(), true));
        g.fillEllipse(screw);
        g.setColour(juce::Colour(0xff0a0a0a));
        g.drawLine(screw.getX() + 1.5f, screw.getBottom() - 2.2f, screw.getRight() - 1.5f, screw.getY() + 2.2f, 0.9f);
    }

    for (const auto& c : captions) {
        switch (c.kind) {
            case CaptionKind::Section: {
                const bool subHeader = c.bounds.getHeight() <= juce::roundToInt(Y(13.0f));
                const auto textArea = c.bounds.withTrimmedLeft(juce::roundToInt(X(subHeader ? 6.0f : 7.0f)));
                if (subHeader) {
                    g.setColour(colours::muted);
                    g.setFont(uiFont(Y(8.0f), true, 0.11f));
                    g.drawFittedText(c.text, textArea, juce::Justification::centredLeft, 1, 0.8f);
                    g.setColour(colours::line);
                    g.drawHorizontalLine(c.bounds.getBottom() - 1, (float) c.bounds.getX() + X(6), (float) c.bounds.getRight() - X(6));
                } else {
                    g.setColour(colours::text);
                    g.setFont(uiFont(Y(10.0f), true, 0.09f));
                    g.drawFittedText(c.text, textArea, juce::Justification::centredLeft, 1, 0.8f);
                }
                break;
            }
            case CaptionKind::Knob:
                g.setColour(colours::muted);
                g.setFont(uiFont(Y(7.5f), true, 0.05f));
                g.drawFittedText(c.text.length() < 12 ? c.text.toUpperCase() : c.text, c.bounds, juce::Justification::centred, 1, 0.75f);
                break;
            case CaptionKind::Side:
                g.setColour(colours::muted);
                g.setFont(uiFont(Y(8.0f), true, 0.08f));
                g.drawFittedText(c.text, c.bounds, juce::Justification::centredLeft, 1, 0.8f);
                break;
        }
    }
}

void WavetableSynthAudioProcessorEditor::resized() {
    captions.clear();
    panels.clear();
    filterEnvelopeGraphBounds = juce::Rectangle<int>();
    wavePreviewBounds.fill(juce::Rectangle<int>());
    hybridPreviewBounds = juce::Rectangle<int>();
    outputMeterBounds = juce::Rectangle<int>();   // индикатор есть только на странице Synth
    oscillatorDropBounds.fill(juce::Rectangle<int>());

    const float sx = (float) getWidth() / 728.0f;
    const float sy = (float) getHeight() / 750.0f;
    const auto X = [sx](int value) { return juce::roundToInt((float) value * sx); };
    const auto Y = [sy](int value) { return juce::roundToInt((float) value * sy); };
    optionsButton.setBounds(X(7), Y(6), X(58), Y(16));
    auto bounds = getLocalBounds();
    const int browserWidth = browserVisible ? X(150) : X(38);
    auto browserColumn = bounds.removeFromLeft(browserWidth);

    if (browserVisible) {
        auto browser = browserColumn.reduced(X(8), Y(53));
        auto browserTitle = browser.removeFromTop(Y(17));
        captions.push_back({ browserTitle.withWidth(juce::jmax(0, browserTitle.getWidth() - X(36))),
                             "WAVEFORM BROWSER", CaptionKind::Section });
        browserToggleButton.setButtonText("HIDE");
        browserToggleButton.setBounds(browserWidth - X(39), Y(53), X(31), Y(16));
        browser.removeFromTop(Y(5));

        auto targetRow = browser.removeFromTop(Y(21));
        const int targetButtonWidth = targetRow.getWidth() / 3;
        targetOsc1Button.setBounds(targetRow.removeFromLeft(targetButtonWidth).reduced(X(1), 0));
        targetOsc2Button.setBounds(targetRow.removeFromLeft(targetButtonWidth).reduced(X(1), 0));
        targetSampleButton.setBounds(targetRow.reduced(X(1), 0));
        browser.removeFromTop(Y(5));
        folderBox.setBounds(browser.removeFromTop(Y(20)));
        browser.removeFromTop(Y(4));
        auto folderButtons = browser.removeFromTop(Y(20));
        addFolderButton.setBounds(folderButtons.removeFromLeft(folderButtons.getWidth() * 3 / 5).reduced(X(1), 0));
        removeFolderButton.setBounds(folderButtons.reduced(X(1), 0));
        browser.removeFromTop(Y(5));

        auto statusArea = browser.removeFromBottom(Y(37));
        status1Label.setBounds(statusArea.removeFromTop(Y(17)));
        status2Label.setBounds(statusArea.removeFromTop(Y(17)));
        folderTree.setBounds(browser);
        hintLabel.setBounds(browser.reduced(X(3)));
    } else {
        browserToggleButton.setButtonText("SHOW");
        browserToggleButton.setBounds(X(3), Y(53), browserWidth - X(6), Y(18));
    }

    juce::Component* browserComponents[] { &targetOsc1Button, &targetOsc2Button, &targetSampleButton, &folderBox,
        &addFolderButton, &removeFolderButton, &folderTree, &hintLabel, &status1Label, &status2Label };
    for (auto* component : browserComponents)
        component->setVisible(browserVisible);
    hintLabel.setVisible(browserVisible && browserFolders.isEmpty());   // подсказка не должна лежать поверх дерева папок

    const int tabsX = browserWidth + X(15);
    synthTabButton.setBounds(tabsX, Y(54), X(49), Y(23));
    effectsTabButton.setBounds(tabsX + X(51), Y(54), X(62), Y(23));

    auto area = bounds.reduced(X(15), Y(6));
    area.removeFromTop(Y(78)); // header, browser title and page tabs

    if (currentPage == 1) {
        addSection("EFFECTS", area.removeFromTop(Y(18)));
        auto cards = area.removeFromTop(Y(162));
        const int gap = X(6);
        const int cardWidth = (cards.getWidth() - 2 * gap) / 3;
        auto makeEffectCard = [&](const juce::String& title, juce::Rectangle<int> card, juce::ToggleButton& on) {
            panels.push_back({ card, juce::Colour(0xff292c2f) });
            auto header = card.removeFromTop(Y(17));
            addSection(title, header.withWidth(juce::jmax(0, header.getWidth() - X(35))));
            on.setBounds(header.removeFromRight(X(34)).reduced(X(1), Y(1)));
            return card.reduced(X(4), Y(3));
        };
        auto reverb = makeEffectCard("REVERB", cards.removeFromLeft(cardWidth), revOnButton);
        cards.removeFromLeft(gap);
        auto delay = makeEffectCard("DELAY", cards.removeFromLeft(cardWidth), delOnButton);
        cards.removeFromLeft(gap);
        auto saturation = makeEffectCard("SATURATION", cards, satOnButton);

        placeCombo(revModeBox, "Mode", reverb.removeFromTop(Y(37)));
        auto revKnobs = reverb.removeFromTop(Y(75));
        const int revCell = revKnobs.getWidth() / 4;
        placeCompactKnob(revSizeSlider, "Size", revKnobs.removeFromLeft(revCell));
        placeCompactKnob(revDecaySlider, "Decay", revKnobs.removeFromLeft(revCell));
        placeCompactKnob(revToneSlider, "Tone", revKnobs.removeFromLeft(revCell));
        placeCompactKnob(revMixSlider, "Mix", revKnobs);

        auto delayControls = delay.removeFromTop(Y(31));
        delSyncButton.setBounds(delayControls.removeFromLeft(delayControls.getWidth() / 2).reduced(X(2), Y(2)));
        placeCombo(delNoteBox, "Rate", delayControls);
        auto delKnobs = delay.removeFromTop(Y(75));
        placeCompactKnob(delToneSlider, "Tone", delKnobs.removeFromLeft(delKnobs.getWidth() / 2));
        placeCompactKnob(delMixSlider, "Mix", delKnobs);

        auto satKnobs = saturation.removeFromTop(Y(90));
        placeCompactKnob(satDriveSlider, "Drive", satKnobs.removeFromLeft(satKnobs.getWidth() / 2));
        placeCompactKnob(satMixSlider, "Mix", satKnobs);
        return;
    }

    // Synth page: three aligned sources, a centered square scope, two LFOs and a complete keyboard row.
    auto sourceRow = area.removeFromTop(Y(210));
    const int sourceGap = X(5);
    const int sourceWidth = (sourceRow.getWidth() - sourceGap * 2) / 3;
    for (int n = 0; n < 2; ++n) {
        if (n > 0) sourceRow.removeFromLeft(sourceGap);
        auto card = sourceRow.removeFromLeft(sourceWidth);
        const int panelX = card.getX(), panelY = card.getY();
        auto& ui = oscUI[(size_t) n];
        auto header = card.removeFromTop(Y(16));
        ui.analogButton.setBounds(header.removeFromRight(X(58)).reduced(X(1), Y(2)));
        ui.enabled.setBounds(header.removeFromRight(X(34)).reduced(X(1), Y(1)));
        addSection("OSCILLATOR " + juce::String(n + 1), header);

        auto topRow = card.removeFromTop(Y(68));
        auto waveColumn = topRow.removeFromLeft(topRow.getWidth() * 45 / 100);
        captions.push_back({ waveColumn.removeFromTop(Y(10)), "WAVEFORM", CaptionKind::Knob });
        ui.wave.setBounds(waveColumn.removeFromTop(Y(18)));
        auto& loadButton = n == 0 ? loadOsc1Button : loadOsc2Button;
        loadButton.setButtonText("LOAD WAV");
        loadButton.setBounds(waveColumn.removeFromTop(Y(15)).reduced(X(1), 0));
        wavePreviewBounds[(size_t) n] = waveColumn.reduced(X(2), Y(1));
        auto largeKnobs = topRow;
        const int largeCell = largeKnobs.getWidth() / 2;
        placeCompactKnob(ui.fine, "Fine", largeKnobs.removeFromLeft(largeCell));
        placeCompactKnob(ui.level, "Level", largeKnobs);

        auto rowA = card.removeFromTop(Y(34));
        const int cellA = rowA.getWidth() / 4;
        placeCompactKnob(ui.wtPos, "WT Pos", rowA.removeFromLeft(cellA));
        placeCompactKnob(ui.oct, "Octave", rowA.removeFromLeft(cellA));
        placeCompactKnob(ui.semi, "Interval", rowA.removeFromLeft(cellA));
        placeCompactKnob(ui.phase, "Phase", rowA);
        auto rowB = card.removeFromTop(Y(34));
        const int cellB = rowB.getWidth() / 4;
        placeCompactKnob(ui.pan, "Pan", rowB.removeFromLeft(cellB));
        placeCompactKnob(ui.unison, "Unison", rowB.removeFromLeft(cellB));
        placeCompactKnob(ui.detune, "Detune", rowB.removeFromLeft(cellB));
        placeCompactKnob(ui.stereo, "Stereo", rowB);
        addSection(u8("AMP ENVELOPE · OSC ") + juce::String(n + 1), card.removeFromTop(Y(12)));
        auto envRow = card.removeFromTop(Y(40));
        const int envCell = envRow.getWidth() / 5;
        placeCompactKnob(ui.attack, "Attack", envRow.removeFromLeft(envCell));
        placeCompactKnob(ui.decay, "Decay", envRow.removeFromLeft(envCell));
        placeCompactKnob(ui.sustain, "Sustain", envRow.removeFromLeft(envCell));
        placeCompactKnob(ui.release, "Release", envRow.removeFromLeft(envCell));
        ui.toFilter.setBounds(envRow.reduced(X(1), Y(8)));
        panels.push_back({ { panelX, panelY, sourceWidth, Y(210) }, juce::Colour(0xff292c2f) });
        oscillatorDropBounds[(size_t) n] = { panelX, panelY, sourceWidth, Y(210) };
    }

    sourceRow.removeFromLeft(sourceGap);
    layoutHybrid(sourceRow.removeFromLeft(sourceWidth).withHeight(Y(210)));

    auto synthRow = area.removeFromTop(Y(218));
    const int scopeWidth = juce::jmin(X(155), synthRow.getWidth() * 28 / 100);
    const int synthGap = X(8);
    const int sideWidth = (synthRow.getWidth() - scopeWidth - synthGap * 2) / 2;
    auto filterG = synthRow.removeFromLeft(sideWidth);
    synthRow.removeFromLeft(synthGap);
    auto scopeG = synthRow.removeFromLeft(scopeWidth);
    synthRow.removeFromLeft(synthGap);
    auto masterG = synthRow;

    auto filterTitle = filterG.removeFromTop(Y(16));
    addSection("FILTER", filterTitle);
    panels.push_back({ { filterG.getX(), filterTitle.getY(), filterG.getWidth(), Y(218) }, juce::Colour(0xff292c2f) });
    auto filterHeader = filterG.removeFromTop(Y(23));
    filterOnButton.setButtonText(u8("ON · BYPASS"));
    filterOnButton.setBounds(filterHeader.removeFromLeft(X(62)).reduced(X(2), Y(1)));
    filterTypeButtons->setBounds(filterHeader.reduced(X(3), Y(2)));
    auto filterControls = filterG.removeFromTop(Y(52));
    placeCompactKnob(cutoffSlider, "Cutoff", filterControls.removeFromLeft(filterControls.getWidth() / 2));
    placeCompactKnob(resonanceSlider, "Resonance", filterControls);
    addSection(u8("FILTER ENVELOPE · ADSR"), filterG.removeFromTop(Y(12)));
    filterEnvelopeGraphBounds = filterG.removeFromTop(Y(34)).reduced(X(5), Y(1));
    auto filterEnvKnobs = filterG.removeFromTop(Y(61));
    const int envFilterCell = filterEnvKnobs.getWidth() / 5;
    placeCompactKnob(filterEnvAttackSlider, "Attack", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvDecaySlider, "Decay", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvSustainSlider, "Sustain", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvReleaseSlider, "Release", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvAmountSlider, "Amount", filterEnvKnobs);

    // осциллограф — экран в своей панели
    panels.push_back({ scopeG.withHeight(Y(218)), juce::Colour() });
    addSection(u8("OSCILLOSCOPE · OUTPUT"), scopeG.removeFromTop(Y(16)));
    oscilloscope.setBounds(scopeG.withTrimmedTop(Y(6)).withHeight(Y(218 - 22 - 6)).reduced(X(6), 0));

    auto masterTitle = masterG.removeFromTop(Y(16));
    addSection("MASTER OUTPUT", masterTitle.withWidth(juce::jmax(0, masterTitle.getWidth() - X(48))));
    captions.push_back({ { masterTitle.getRight() - X(46), masterTitle.getY(), X(46), masterTitle.getHeight() },
                         "STEREO", CaptionKind::Side });
    panels.push_back({ { masterG.getX(), masterTitle.getY(), masterG.getWidth(), Y(218) }, juce::Colour(0xff292c2f) });
    auto meterCell = masterG.removeFromRight(X(48));
    outputMeterBounds = meterCell.withHeight(Y(112)).withCentre({ meterCell.getCentreX(), masterG.getCentreY() });
    const int masterCell = masterG.getWidth() / 2;
    auto masterControls = masterG.withHeight(Y(72)).withCentre({ masterG.getCentreX(), masterG.getCentreY() });
    placeCompactKnob(masterVolumeSlider, "Volume", masterControls.removeFromLeft(masterCell));
    placeCompactKnob(masterPanSlider, "Pan", masterControls);

    // LFO 1 · LFO 2 · ARPEGGIATOR в одном ряду
    area.removeFromTop(Y(6));
    auto modRow = area.removeFromTop(Y(112));
    const int modGap = X(8), arpWidth = X(150);
    const int lfoWidth = (modRow.getWidth() - arpWidth - modGap * 2) / 2;
    auto placeChoicePair = [&](juce::Rectangle<int> row, juce::ComboBox& left, const char* leftName,
                               juce::ComboBox& right, const char* rightName) {
        row = row.reduced(X(4), 0);
        auto leftCell = row.removeFromLeft(row.getWidth() / 2);
        captions.push_back({ leftCell.removeFromTop(Y(10)), leftName, CaptionKind::Knob });
        left.setBounds(leftCell.removeFromTop(Y(20)).reduced(X(1), 0));
        captions.push_back({ row.removeFromTop(Y(10)), rightName, CaptionKind::Knob });
        right.setBounds(row.removeFromTop(Y(20)).reduced(X(1), 0));
    };
    for (int n = 0; n < 2; ++n) {
        auto card = modRow.removeFromLeft(lfoWidth);
        modRow.removeFromLeft(modGap);
        panels.push_back({ card, juce::Colour() });
        auto& ui = lfoUI[(size_t) n];
        auto header = card.removeFromTop(Y(16));
        ui.enabled.setBounds(header.removeFromRight(X(36)).reduced(X(1), Y(1)));
        ui.tempoSync.setBounds(header.removeFromRight(X(46)).reduced(X(1), Y(1)));
        addSection("LFO " + juce::String(n + 1), header);
        placeChoicePair(card.removeFromTop(Y(32)), ui.destination, "Dest", ui.waveform, "Shape");
        auto controls = card.reduced(X(4), 0);
        placeCompactKnob(ui.rate, "Rate", controls.removeFromLeft(X(56)));
        auto division = controls.removeFromLeft(X(60)).reduced(X(2), 0);
        captions.push_back({ division.withY(division.getY() + Y(4)).withHeight(Y(10)), "Div", CaptionKind::Knob });
        ui.syncDivision.setBounds(division.withY(division.getY() + Y(16)).withHeight(Y(20)));
        placeCompactKnob(ui.depth, "Depth", controls);
    }
    {
        auto card = modRow;
        panels.push_back({ card, juce::Colour() });
        auto header = card.removeFromTop(Y(16));
        arpEnableButton.setBounds(header.removeFromRight(X(36)).reduced(X(1), Y(1)));
        addSection("ARPEGGIATOR", header);
        placeChoicePair(card.removeFromTop(Y(32)), arpModeBox, "Mode", arpRateBox, "Rate");
        auto controls = card.reduced(X(4), 0);
        placeCompactKnob(arpOctSlider, "Octaves", controls.removeFromLeft(controls.getWidth() / 2));
        placeCompactKnob(arpGateSlider, "Gate", controls);
    }

    area.removeFromTop(Y(7));
    auto performance = area.removeFromTop(Y(107));
    panels.push_back({ performance, juce::Colour(0xff292c2f) });
    addSection("PERFORMANCE", performance.removeFromTop(Y(14)));
    auto wheels = performance.removeFromLeft(X(70));
    const int wheelsX = wheels.getX();
    auto wheelTitle = wheels.removeFromTop(Y(10));
    captions.push_back({ wheelTitle, "WHEELS", CaptionKind::Knob });
    auto pitchCell = wheels.removeFromLeft(wheels.getWidth() / 2);
    captions.push_back({ pitchCell.removeFromTop(Y(9)), "PITCH", CaptionKind::Knob });
    pitchWheelSlider.setBounds(pitchCell.removeFromTop(Y(53)).reduced(X(5), 0));
    auto modCell = wheels;
    captions.push_back({ modCell.removeFromTop(Y(9)), "MOD", CaptionKind::Knob });
    modWheelSlider.setBounds(modCell.removeFromTop(Y(53)).reduced(X(5), 0));
    pitchRangeBox.setBounds({ wheelsX + X(2), wheels.getBottom() - Y(18), X(66), Y(17) });

    performance.removeFromLeft(X(5));
    auto voice = performance.removeFromRight(X(92));
    performance.removeFromRight(X(5));
    keyboard.setBounds(performance.reduced(X(2), Y(4)));
    addSection("VOICE", { voice.getX(), voice.getY(), voice.getWidth(), Y(14) });
    auto voiceChoices = voice.withY(voice.getY() + Y(17)).withHeight(Y(22));
    voiceModeBox.setBounds(voiceChoices.removeFromLeft(voiceChoices.getWidth() / 2).reduced(X(1), 0));
    voiceCountBox.setBounds(voiceChoices.reduced(X(1), 0));
    placeCompactKnob(glideSlider, "Glide", voice.withY(voice.getY() + Y(38)).withHeight(Y(57)));
}

// Карточка гибридного источника: общий верх и низ, а середина зависит от режима SUB / NOISE / SAMPLE.
void WavetableSynthAudioProcessorEditor::layoutHybrid(juce::Rectangle<int> card) {
    const float sx = (float) getWidth() / 728.0f;
    const float sy = (float) getHeight() / 750.0f;
    const auto X = [sx](int value) { return juce::roundToInt((float) value * sx); };
    const auto Y = [sy](int value) { return juce::roundToInt((float) value * sy); };
    const auto full = card;
    const int mode = hybridModeButtons->getSelected();
    const bool isSub = mode == 0, isNoise = mode == 1, isSample = mode == 2;

    subShapeButtons->setVisible(isSub);
    subOctaveSlider.setVisible(isSub);
    noiseColourButtons->setVisible(isNoise);
    noiseTiltSlider.setVisible(isNoise);
    noiseStereoSlider.setVisible(isNoise);
    for (juce::Component* c : { (juce::Component*) &sampleDisplay, (juce::Component*) &sampleLoopButton,
                                (juce::Component*) &sampleReverseButton, (juce::Component*) &sampleKeytrackButton,
                                (juce::Component*) &sampleRootBox, (juce::Component*) &samplePitchSlider,
                                (juce::Component*) &sampleStartSlider })
        c->setVisible(isSample);
    hybridFineSlider.setVisible(isSub || isSample);
    hybridPanSlider.setVisible(isSub || isNoise);

    auto header = card.removeFromTop(Y(16));
    subEnabledButton.setBounds(header.removeFromRight(X(42)).reduced(X(1), Y(1)));
    addSection("HYBRID SOURCE", header);
    hybridModeButtons->setBounds(card.removeFromTop(Y(20)).reduced(X(5), Y(2)));
    card.removeFromTop(Y(2));

    auto body = card.removeFromTop(Y(88));
    std::array<std::pair<juce::Slider*, const char*>, 4> knobs;
    if (isSample) {
        const auto display = body.removeFromTop(Y(46)).reduced(X(5), 0);
        sampleDisplay.setBounds(display);
        sampleRootBox.setBounds(display.getRight() - X(54), display.getY() + Y(1), X(53), Y(13));
        auto toggles = display.withTop(display.getBottom() - Y(13)).reduced(X(2), 0);
        sampleLoopButton.setBounds(toggles.removeFromLeft(X(44)));
        sampleReverseButton.setBounds(toggles.removeFromLeft(X(40)));
        sampleKeytrackButton.setBounds(toggles.removeFromLeft(X(40)));
        knobs = { { { &samplePitchSlider, "Pitch" }, { &hybridFineSlider, "Fine" },
                    { &sampleStartSlider, "Start" }, { &subLevelSlider, "Level" } } };
    } else {
        const auto selector = body.removeFromTop(Y(16)).reduced(X(5), Y(1));
        (isSub ? *subShapeButtons : *noiseColourButtons).setBounds(selector);
        body.removeFromTop(Y(2));
        hybridPreviewBounds = body.removeFromTop(Y(28)).reduced(X(5), 0);
        if (isSub)
            knobs = { { { &subOctaveSlider, "Octave" }, { &hybridFineSlider, "Fine" },
                        { &hybridPanSlider, "Pan" }, { &subLevelSlider, "Level" } } };
        else
            knobs = { { { &noiseTiltSlider, "Tilt" }, { &noiseStereoSlider, "Stereo" },
                        { &hybridPanSlider, "Pan" }, { &subLevelSlider, "Level" } } };
    }
    const int knobCell = body.getWidth() / 4;
    for (size_t k = 0; k < knobs.size(); ++k)
        placeCompactKnob(*knobs[k].first, knobs[k].second, k < 3 ? body.removeFromLeft(knobCell) : body);

    addSection("AMP ENVELOPE", card.removeFromTop(Y(12)));
    auto env = card.removeFromTop(Y(40));
    const int envCell = env.getWidth() / 4;
    placeCompactKnob(subAttackSlider, "Attack", env.removeFromLeft(envCell));
    placeCompactKnob(subDecaySlider, "Decay", env.removeFromLeft(envCell));
    placeCompactKnob(subSustainSlider, "Sustain", env.removeFromLeft(envCell));
    placeCompactKnob(subReleaseSlider, "Release", env);

    card.removeFromTop(Y(3));
    auto route = card.removeFromTop(Y(18)).reduced(X(5), 0);
    captions.push_back({ route.removeFromLeft(X(36)), "ROUTE", CaptionKind::Side });
    hybridRouteButtons->setBounds(route);

    panels.push_back({ full, juce::Colour(0xff292c2f) });
    oscillatorDropBounds[2] = full;
}

// Превью для режимов SUB (форма волны) и NOISE (наклон спектра трёх цветов шума)
void WavetableSynthAudioProcessorEditor::paintHybridPreview(juce::Graphics& g) {
    if (hybridPreviewBounds.isEmpty()) return;
    const auto area = hybridPreviewBounds.toFloat();
    const auto accent = unclaw::colours::cyan;
    unclaw::drawScreen(g, area, 6, 2, 3.0f);
    g.setFont(unclaw::uiFont(7.0f, true, 0.08f));
    const auto inner = area.reduced(4.0f, 4.0f);

    if (hybridModeButtons->getSelected() == 0) {
        const bool square = subShapeButtons->getSelected() == 1;
        juce::Path wave;
        for (int i = 0; i <= 64; ++i) {
            const float t = (float) i / 64.0f;
            const float v = square ? (t < 0.5f ? 0.8f : -0.8f) : 0.8f * std::sin(juce::MathConstants<float>::twoPi * t);
            const juce::Point<float> pt(inner.getX() + inner.getWidth() * t, inner.getCentreY() - v * inner.getHeight() * 0.5f);
            if (i == 0) wave.startNewSubPath(pt); else wave.lineTo(pt);
        }
        unclaw::strokeGlow(g, wave, accent, 1.2f, { { 3.2f, 0.12f }, { 1.8f, 0.25f } });
        g.setColour(unclaw::colours::muted);
        g.drawText(subOctaveSlider.getTextFromValue(subOctaveSlider.getValue()).toUpperCase(),
                   hybridPreviewBounds.reduced(4, 2), juce::Justification::topRight);
        return;
    }

    const int colour = noiseColourButtons->getSelected();
    const float tiltDb = (float) noiseTiltSlider.getValue();
    const float tiltShift = -tiltDb / 12.0f * inner.getHeight() * 0.3f;   // + Tilt поднимает правый край
    for (int c = 0; c < 3; ++c) {
        const float drop = inner.getHeight() * 0.42f * (float) c;
        const float y0 = inner.getY() + inner.getHeight() * 0.2f - tiltShift;
        const float y1 = y0 + drop + 2.0f * tiltShift;
        juce::Path slope;
        slope.startNewSubPath(inner.getX(), juce::jlimit(inner.getY(), inner.getBottom(), y0));
        slope.lineTo(inner.getRight(), juce::jlimit(inner.getY(), inner.getBottom(), y1));
        if (c == colour) {
            auto fill = slope;
            fill.lineTo(inner.getRight(), inner.getBottom());
            fill.lineTo(inner.getX(), inner.getBottom());
            fill.closeSubPath();
            g.setColour(accent.withAlpha(0.10f));
            g.fillPath(fill);
            unclaw::strokeGlow(g, slope, accent, 1.2f, { { 3.0f, 0.15f } });
        } else {
            juce::Path dashed;
            const float dashes[] { 2.0f, 2.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, slope, dashes, 2);
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.fillPath(dashed);
        }
    }
    static const char* slopes[] { "0 dB/oct", "-3 dB/oct", "-6 dB/oct" };
    g.setColour(accent);
    g.setFont(unclaw::monoFont(7.0f));
    g.drawText(slopes[juce::jlimit(0, 2, colour)], hybridPreviewBounds.reduced(4, 2), juce::Justification::bottomRight);
}

void WavetableSynthAudioProcessorEditor::updateSampleDisplay() {
    const int version = audioProcessor.getHybridSampleVersion();
    const float start = (float) sampleStartSlider.getValue();
    const bool loop = sampleLoopButton.getToggleState(), reverse = sampleReverseButton.getToggleState();
    const auto key = juce::String(version) + "|" + juce::String(start, 3) + "|" + juce::String((int) loop) + juce::String((int) reverse);
    if (key == lastSampleKey) return;
    lastSampleKey = key;

    auto view = audioProcessor.getHybridSampleView();
    SampleDisplay::State state;
    state.peaks = std::move(view.peaks);
    state.name = view.name;
    state.seconds = view.seconds;
    state.status = audioProcessor.getHybridStatus();
    state.start = start;
    state.loop = loop;
    state.reverse = reverse;
    sampleDisplay.setState(std::move(state));
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
            if (file.existsAsFile()) safeThis->loadExternalWavetable(file, targetOsc);
        });
}
