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

    optionsButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff242629));
    optionsButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff684e2a));
    optionsButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe6e4dd));
    optionsButton.onClick = [this] {
        juce::PopupMenu sizes;
        sizes.addItem(1, "Compact  ·  80%", true, getWidth() < 850);
        sizes.addItem(2, "Standard  ·  100%", true, getWidth() >= 850 && getWidth() < 1020);
        sizes.addItem(3, "Large  ·  120%", true, getWidth() >= 1020);
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

    for (auto* tab : { &synthTabButton, &effectsTabButton, &arpTabButton }) {
        tab->setClickingTogglesState(false);
        tab->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2023));
        tab->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff684e2a));
        tab->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaa9a2));
        tab->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffdc86));
        addAndMakeVisible(tab);
    }
    synthTabButton.onClick = [this] { showPage(0); };
    effectsTabButton.onClick = [this] { showPage(1); };
    arpTabButton.onClick = [this] { showPage(2); };

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
    addCombo(subWaveBox, "SUB_WAVE");
    addKnob(subLevelSlider, "SUB_LEVEL");
    addToggle(subEnabledButton, "SUB_ON");
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
    currentPage = juce::jlimit(0, 2, page);
    synthTabButton.setToggleState(currentPage == 0, juce::dontSendNotification);
    effectsTabButton.setToggleState(currentPage == 1, juce::dontSendNotification);
    arpTabButton.setToggleState(currentPage == 2, juce::dontSendNotification);

    const bool showSynth = currentPage == 0;
    const bool showEffects = currentPage == 1;
    const bool showArp = currentPage == 2;
    auto setVisible = [] (juce::Component& component, bool visible) { component.setVisible(visible); };
    auto setComponentsVisible = [&] (bool visible, auto&... components) {
        (setVisible(components, visible), ...);
    };

    for (auto& osc : oscUI) {
        setComponentsVisible(showSynth, osc.wave, osc.wtPos, osc.level, osc.oct, osc.semi, osc.fine,
                             osc.phase, osc.pan, osc.unison, osc.detune, osc.stereo, osc.attack,
                             osc.decay, osc.sustain, osc.release, osc.analogButton, osc.enabled, osc.toFilter);
    }
    setComponentsVisible(showSynth, loadOsc1Button, loadOsc2Button, oscilloscope, keyboard, subWaveBox,
                         subLevelSlider, subAttackSlider, subDecaySlider, subSustainSlider,
                         subReleaseSlider, subEnabledButton, subToFilterButton, cutoffSlider, resonanceSlider,
                         filterOnButton, filterTypeBox, filterEnvAttackSlider, filterEnvDecaySlider,
                         filterEnvSustainSlider, filterEnvReleaseSlider, filterEnvAmountSlider,
                         masterVolumeSlider, masterPanSlider, pitchWheelSlider, modWheelSlider,
                         pitchRangeBox, voiceModeBox, voiceCountBox, glideSlider);
    for (auto& lfo : lfoUI)
        setComponentsVisible(showSynth, lfo.enabled, lfo.tempoSync, lfo.destination,
                             lfo.waveform, lfo.syncDivision, lfo.rate, lfo.depth);
    setComponentsVisible(showEffects, revModeBox, revSizeSlider, revDecaySlider, revToneSlider,
                         revMixSlider, revOnButton, delSyncButton, delNoteBox, delToneSlider, delMixSlider,
                         delOnButton, satDriveSlider, satMixSlider, satOnButton);
    setComponentsVisible(showArp, arpEnableButton, arpModeBox, arpRateBox, arpOctSlider, arpGateSlider);
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
    const auto point = details.localPosition;
    int target = 0;
    for (int i = 0; i < (int) oscillatorDropBounds.size(); ++i)
        if (oscillatorDropBounds[(size_t) i].contains(point)) target = i + 1;
    if (target != dragHoverOscillator) {
        dragHoverOscillator = target;
        repaint();
    }
}

void WavetableSynthAudioProcessorEditor::itemDragExit(const SourceDetails&) {
    if (dragHoverOscillator != 0) {
        dragHoverOscillator = 0;
        repaint();
    }
}

void WavetableSynthAudioProcessorEditor::itemDropped(const SourceDetails& details) {
    const auto point = details.localPosition;
    for (int i = 0; i < (int) oscillatorDropBounds.size(); ++i) {
        if (oscillatorDropBounds[(size_t) i].contains(point)) {
            loadExternalWavetable(juce::File(details.description.toString()), i + 1);
            break;
        }
    }
    dragHoverOscillator = 0;
    repaint();
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
    loadExternalWavetable(file, targetOsc);
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
    revOnButton.setButtonText(revOnButton.getToggleState() ? "ON" : "OFF");
    delOnButton.setButtonText(delOnButton.getToggleState() ? "ON" : "OFF");
    satOnButton.setButtonText(satOnButton.getToggleState() ? "ON" : "OFF");
    repaint(outputMeterBounds.expanded(4));
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
    if (dragHoverOscillator > 0 && dragHoverOscillator <= (int) oscillatorDropBounds.size()) {
        const auto target = oscillatorDropBounds[(size_t) dragHoverOscillator - 1].toFloat();
        g.setColour(juce::Colour(0xffffd02a).withAlpha(0.10f));
        g.fillRoundedRectangle(target, 5.0f);
        g.setColour(juce::Colour(0xffffd02a));
        g.drawRoundedRectangle(target.reduced(2.0f), 5.0f, 2.0f);
    }
    if (!filterEnvelopeGraphBounds.isEmpty()) {
        const auto graph = filterEnvelopeGraphBounds.toFloat();
        g.setColour(juce::Colour(0xff101214));
        g.fillRoundedRectangle(graph, 3.0f);
        g.setColour(juce::Colour(0xff45494d));
        g.drawRoundedRectangle(graph, 3.0f, 1.0f);
        for (int i = 1; i < 4; ++i) {
            const float x = graph.getX() + graph.getWidth() * (float) i / 4.0f;
            g.drawVerticalLine((int) x, graph.getY() + 2.0f, graph.getBottom() - 2.0f);
        }
        g.setColour(juce::Colour(0xffffc928));
        juce::Path envelope;
        envelope.startNewSubPath(graph.getX() + 3.0f, graph.getBottom() - 3.0f);
        envelope.lineTo(graph.getX() + graph.getWidth() * 0.18f, graph.getY() + 4.0f);
        envelope.lineTo(graph.getX() + graph.getWidth() * 0.40f, graph.getY() + graph.getHeight() * 0.50f);
        envelope.lineTo(graph.getX() + graph.getWidth() * 0.72f, graph.getY() + graph.getHeight() * 0.66f);
        envelope.lineTo(graph.getRight() - 3.0f, graph.getBottom() - 3.0f);
        g.strokePath(envelope, juce::PathStrokeType(1.5f));
    }
    for (size_t osc = 0; osc < wavePreviewBounds.size(); ++osc) {
        const auto area = wavePreviewBounds[osc].toFloat();
        if (area.isEmpty()) continue;
        g.setColour(juce::Colour(0xff111416));
        g.fillRoundedRectangle(area, 3.0f);
        g.setColour(juce::Colour(0xff45494d));
        g.drawRoundedRectangle(area, 3.0f, 1.0f);
        const auto samples = audioProcessor.getWavetablePreview((int) osc + 1);
        juce::Path wave;
        for (size_t i = 0; i < samples.size(); ++i) {
            const float x = area.getX() + 3.0f + (area.getWidth() - 6.0f) * (float) i / (float) (samples.size() - 1);
            const float y = area.getCentreY() - samples[i] * (area.getHeight() * 0.38f);
            if (i == 0) wave.startNewSubPath(x, y); else wave.lineTo(x, y);
        }
        g.setColour(juce::Colour(0xffffc928));
        g.strokePath(wave, juce::PathStrokeType(1.25f));
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
    // Header and browser rail follow the supplied compact reference layout.
    const float sx = (float) getWidth() / 728.0f;
    const float sy = (float) getHeight() / 750.0f;
    const auto X = [sx](float value) { return value * sx; };
    const auto Y = [sy](float value) { return value * sy; };
    g.setColour(juce::Colour(0xff151719));
    g.fillRoundedRectangle(X(6), Y(5), (float) getWidth() - X(12), Y(43), 5.0f);
    g.setColour(juce::Colour(0xff45494c));
    g.drawRoundedRectangle(X(6), Y(5), (float) getWidth() - X(12), Y(43), 5.0f, 1.0f);
    juce::Path mark;
    mark.startNewSubPath(X(77), Y(32)); mark.lineTo(X(87), Y(11)); mark.lineTo(X(97), Y(32));
    mark.lineTo(X(91), Y(29)); mark.lineTo(X(83), Y(32));
    g.setColour(juce::Colour(0xffffc928));
    g.strokePath(mark, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour(0xffe7e3d8));
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("UNCLAW", (int) X(103), (int) Y(10), (int) X(80), (int) Y(18), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xffffc928));
    g.setFont(juce::FontOptions(7.0f, juce::Font::bold));
    g.drawText("WAVETABLE SYNTHESIZER", (int) X(104), (int) Y(27), (int) X(120), (int) Y(10), juce::Justification::centredLeft);
    g.setColour(juce::Colour(0xff383b3e));
    const float rail = browserVisible ? X(158) : X(46);
    g.fillRoundedRectangle(X(6), Y(53), rail - X(9), (float) getHeight() - Y(59), 3.0f);
    g.drawVerticalLine(rail, Y(52), (float) getHeight() - Y(8));

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
    filterEnvelopeGraphBounds = juce::Rectangle<int>();
    wavePreviewBounds.fill(juce::Rectangle<int>());
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
        captions.push_back({ targetRow.removeFromLeft(X(44)), "Load into:", CaptionKind::Side });
        const int targetButtonWidth = targetRow.getWidth() / 2;
        targetOsc1Button.setBounds(targetRow.removeFromLeft(targetButtonWidth).reduced(X(1), 0));
        targetOsc2Button.setBounds(targetRow.reduced(X(1), 0));
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

    juce::Component* browserComponents[] { &targetOsc1Button, &targetOsc2Button, &folderBox,
        &addFolderButton, &removeFolderButton, &folderTree, &hintLabel, &status1Label, &status2Label };
    for (auto* component : browserComponents)
        component->setVisible(browserVisible);

    const int tabsX = browserWidth + X(15);
    synthTabButton.setBounds(tabsX, Y(54), X(49), Y(23));
    effectsTabButton.setBounds(tabsX + X(51), Y(54), X(62), Y(23));
    arpTabButton.setBounds(tabsX + X(115), Y(54), X(84), Y(23));

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

    if (currentPage == 2) {
        addSection("ARPEGGIATOR", area.removeFromTop(Y(18)));
        auto arpPanel = area.removeFromTop(Y(130));
        panels.push_back({ arpPanel, juce::Colour(0xff292c2f) });
        auto controls = arpPanel.reduced(X(8), Y(8));
        arpEnableButton.setBounds(controls.removeFromLeft(X(82)).withHeight(Y(22)));
        controls.removeFromLeft(X(8));
        placeCombo(arpModeBox, "Mode", controls.removeFromLeft(X(122)));
        controls.removeFromLeft(X(6));
        placeCombo(arpRateBox, "Rate", controls.removeFromLeft(X(94)));
        controls.removeFromLeft(X(6));
        placeCompactKnob(arpOctSlider, "Octaves", controls.removeFromLeft(X(54)));
        placeCompactKnob(arpGateSlider, "Gate", controls.removeFromLeft(X(54)));
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
        ui.analogButton.setBounds(header.removeFromRight(X(56)).reduced(X(1), Y(1)));
        ui.enabled.setBounds(header.removeFromRight(X(27)).reduced(X(1), Y(1)));
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
        addSection("AMP ENVELOPE · OSC " + juce::String(n + 1), card.removeFromTop(Y(12)));
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
    auto subCard = sourceRow.removeFromLeft(sourceWidth);
    const int subX = subCard.getX(), subY = subCard.getY();
    auto subHeader = subCard.removeFromTop(Y(16));
    subEnabledButton.setBounds(subHeader.removeFromRight(X(29)).reduced(X(1), Y(1)));
    addSection("SUB OSCILLATOR", subHeader);
    auto subControls = subCard.removeFromTop(Y(68));
    auto subWaveCell = subControls.removeFromLeft(subControls.getWidth() * 42 / 100);
    captions.push_back({ subWaveCell.removeFromTop(Y(10)), "WAVE", CaptionKind::Knob });
    subWaveBox.setBounds(subWaveCell.removeFromTop(Y(18)));
    placeCompactKnob(subLevelSlider, "Level", subControls.removeFromLeft(subControls.getWidth() / 2));
    subToFilterButton.setBounds(subControls.reduced(X(1), Y(12)));
    addSection("AMP ENVELOPE", subCard.removeFromTop(Y(12)));
    auto subEnv = subCard.removeFromTop(Y(44));
    const int subEnvCell = subEnv.getWidth() / 4;
    placeCompactKnob(subAttackSlider, "Attack", subEnv.removeFromLeft(subEnvCell));
    placeCompactKnob(subDecaySlider, "Decay", subEnv.removeFromLeft(subEnvCell));
    placeCompactKnob(subSustainSlider, "Sustain", subEnv.removeFromLeft(subEnvCell));
    placeCompactKnob(subReleaseSlider, "Release", subEnv);
    captions.push_back({ { subX + X(8), subY + Y(187), sourceWidth - X(16), Y(11) },
                         "TUNED ONE OCTAVE BELOW", CaptionKind::Side });
    panels.push_back({ { subX, subY, sourceWidth, Y(210) }, juce::Colour(0xff292c2f) });

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
    filterOnButton.setButtonText("ON · BYPASS");
    filterOnButton.setBounds(filterHeader.removeFromLeft(X(56)).reduced(X(1), Y(1)));
    filterTypeBox.setBounds(filterHeader.reduced(X(1), Y(1)));
    auto filterControls = filterG.removeFromTop(Y(52));
    placeCompactKnob(cutoffSlider, "Cutoff", filterControls.removeFromLeft(filterControls.getWidth() / 2));
    placeCompactKnob(resonanceSlider, "Resonance", filterControls);
    addSection("FILTER ENVELOPE · ADSR", filterG.removeFromTop(Y(12)));
    filterEnvelopeGraphBounds = filterG.removeFromTop(Y(34)).reduced(X(5), Y(1));
    auto filterEnvKnobs = filterG.removeFromTop(Y(61));
    const int envFilterCell = filterEnvKnobs.getWidth() / 5;
    placeCompactKnob(filterEnvAttackSlider, "Attack", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvDecaySlider, "Decay", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvSustainSlider, "Sustain", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvReleaseSlider, "Release", filterEnvKnobs.removeFromLeft(envFilterCell));
    placeCompactKnob(filterEnvAmountSlider, "Amount", filterEnvKnobs);

    addSection("OSCILLOSCOPE · OUTPUT - STEREO", scopeG.removeFromTop(Y(16)));
    const int scopeSide = juce::jmin(scopeG.getWidth(), scopeG.getHeight());
    oscilloscope.setBounds(scopeG.withSizeKeepingCentre(scopeSide, scopeSide));

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

    area.removeFromTop(Y(6));
    auto lfoRow = area.removeFromTop(Y(112));
    const int lfoGap = X(8);
    const int lfoWidth = (lfoRow.getWidth() - lfoGap) / 2;
    for (int n = 0; n < 2; ++n) {
        if (n > 0) lfoRow.removeFromLeft(lfoGap);
        auto card = lfoRow.removeFromLeft(lfoWidth);
        const int cardX = card.getX(), cardY = card.getY();
        auto& ui = lfoUI[(size_t) n];
        auto header = card.removeFromTop(Y(16));
        ui.enabled.setBounds(header.removeFromRight(X(38)).reduced(X(1), Y(1)));
        addSection("LFO " + juce::String(n + 1), { cardX, cardY, lfoWidth - X(40), Y(16) });
        auto targetRow = card.removeFromTop(Y(32));
        const int targetWidth = targetRow.getWidth() / 2;
        captions.push_back({ targetRow.removeFromTop(Y(10)), "DEST", CaptionKind::Knob });
        ui.destination.setBounds(targetRow.removeFromTop(Y(20)).withWidth(targetWidth - X(3)));
        auto shapeCell = juce::Rectangle<int>(cardX + targetWidth, cardY + Y(16), targetWidth, Y(32));
        captions.push_back({ shapeCell.removeFromTop(Y(10)), "SHAPE", CaptionKind::Knob });
        ui.waveform.setBounds(shapeCell.removeFromTop(Y(20)).reduced(X(1), 0));
        auto controls = card;
        const int controlCell = juce::jmax(X(40), controls.getWidth() / 5);
        placeCompactKnob(ui.rate, "Rate (Hz)", controls.removeFromLeft(controlCell));
        ui.tempoSync.setBounds(controls.removeFromLeft(X(48)).withHeight(Y(20)).withY(controls.getY() + Y(14)));
        auto tempo = controls.removeFromLeft(X(72));
        captions.push_back({ tempo.removeFromTop(Y(10)), "DIV", CaptionKind::Knob });
        ui.syncDivision.setBounds(tempo.removeFromTop(Y(20)));
        placeCompactKnob(ui.depth, "Depth", controls);
        panels.push_back({ { cardX, cardY, lfoWidth, Y(112) }, juce::Colour(0xff292c2f) });
    }

    area.removeFromTop(Y(7));
    auto performance = area.removeFromTop(Y(107));
    panels.push_back({ performance, juce::Colour(0xff292c2f) });
    addSection("PERFORMANCE", performance.removeFromTop(Y(14)));
    auto wheels = performance.removeFromLeft(X(70));
    auto wheelTitle = wheels.removeFromTop(Y(10));
    captions.push_back({ wheelTitle, "WHEELS", CaptionKind::Knob });
    auto pitchCell = wheels.removeFromLeft(wheels.getWidth() / 2);
    captions.push_back({ pitchCell.removeFromTop(Y(9)), "PITCH", CaptionKind::Knob });
    pitchWheelSlider.setBounds(pitchCell.removeFromTop(Y(53)).reduced(X(5), 0));
    auto modCell = wheels;
    captions.push_back({ modCell.removeFromTop(Y(9)), "MOD", CaptionKind::Knob });
    modWheelSlider.setBounds(modCell.removeFromTop(Y(53)).reduced(X(5), 0));
    pitchRangeBox.setBounds({ wheels.getX(), wheels.getBottom() - Y(18), X(66), Y(17) });

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
