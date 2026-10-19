#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "OscilloscopeDisplay.h"
#include "BrowserSettings.h"
#include <array>
#include <vector>

class WavetableSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           public juce::Button::Listener,
                                           public juce::FileBrowserListener,
                                           private juce::Timer {
public:
    WavetableSynthAudioProcessorEditor(WavetableSynthAudioProcessor&);
    ~WavetableSynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void buttonClicked(juce::Button* button) override;
    void showPage(int page);

    // juce::FileBrowserListener — выбор файла в дереве браузера
    void selectionChanged() override;
    void fileClicked(const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked(const juce::File&) override {}
    void browserRootChanged(const juce::File&) override {}

private:
    void timerCallback() override;
    void refreshFolderBox(int selectIndex);
    void showFolder(int index);
    void loadFromBrowser(const juce::File& file);
    void loadExternalWavetable(const juce::File& file, int oscillator);

    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttachment = APVTS::SliderAttachment;
    using ButtonAttachment = APVTS::ButtonAttachment;
    using ComboAttachment = APVTS::ComboBoxAttachment;

    enum class CaptionKind { Section, Knob, Side };
    struct Caption { juce::Rectangle<int> bounds; juce::String text; CaptionKind kind; };
    struct Panel { juce::Rectangle<int> bounds; juce::Colour colour; };
    std::vector<Caption> captions;
    std::vector<Panel> panels;

    void placeKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell);
    void placeCompactKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell);
    void placeCombo(juce::ComboBox& box, const juce::String& name, juce::Rectangle<int> cell);
    void placeHFader(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> row);
    void addSection(const juce::String& name, juce::Rectangle<int> bounds);

    WavetableSynthAudioProcessor& audioProcessor;
    OscilloscopeDisplay oscilloscope;
    juce::MidiKeyboardComponent keyboard;

    juce::TextButton loadOsc1Button { "Load WAV..." };
    juce::TextButton loadOsc2Button { "Load WAV..." };
    juce::TextButton synthTabButton { "SYNTH" }, effectsTabButton { "EFFECTS" }, arpTabButton { "ARPEGGIATOR" };
    int currentPage = 0;

    // Панель осциллятора: форма, позиция в таблице, громкость, высота, фаза, панорама, юнисон
    struct OscUI {
        juce::ComboBox wave;
        juce::Slider wtPos, level, oct, semi, fine, phase, pan, unison, detune, stereo;
        juce::Slider attack, decay, sustain, release;
        juce::TextButton analogButton { "ANALOG MODE" };
        juce::ToggleButton toFilter { "TO FILTER" };
    };
    std::array<OscUI, 2> oscUI;
    std::array<juce::Rectangle<int>, 2> wavePreviewBounds;

    juce::ComboBox subWaveBox;
    juce::Slider subLevelSlider;
    juce::Slider subAttackSlider, subDecaySlider, subSustainSlider, subReleaseSlider;
    juce::ToggleButton subToFilterButton { "TO FILTER" };
    juce::Slider cutoffSlider, resonanceSlider;
    juce::ToggleButton filterOnButton { "FILTER ON" };
    juce::ComboBox filterTypeBox;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;   // горизонтальные фейдеры
    juce::Slider filterEnvAttackSlider, filterEnvDecaySlider, filterEnvSustainSlider,
                  filterEnvReleaseSlider, filterEnvAmountSlider;
    juce::Slider masterVolumeSlider, masterPanSlider;
    juce::Rectangle<int> outputMeterBounds;
    juce::Rectangle<int> filterEnvelopeGraphBounds;
    struct LfoUI {
        juce::ToggleButton enabled { "ON" }, tempoSync { "SYNC" };
        juce::ComboBox destination, waveform, syncDivision;
        juce::Slider rate, depth;
    };
    std::array<LfoUI, 2> lfoUI;
    juce::ComboBox voiceModeBox, voiceCountBox;
    juce::Slider glideSlider;
    juce::Slider pitchWheelSlider, modWheelSlider;
    juce::ComboBox pitchRangeBox;
    juce::ComboBox revModeBox;
    juce::ToggleButton revOnButton { "ON" }, delOnButton { "ON" }, satOnButton { "ON" };
    juce::Slider revSizeSlider, revDecaySlider, revToneSlider, revMixSlider;
    juce::ToggleButton delSyncButton { "Sync" };
    juce::ComboBox delNoteBox;
    juce::Slider delToneSlider, delMixSlider;
    juce::Slider satDriveSlider, satMixSlider;
    juce::ToggleButton arpEnableButton { "Arp On" };
    juce::ComboBox arpModeBox, arpRateBox;
    juce::Slider arpOctSlider, arpGateSlider;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::FileChooser> folderChooser;

    // Браузер вейвтейблов (слева). Порядок объявления важен: поток сканирования должен пережить список и дерево.
    BrowserSettings browserSettings;
    juce::StringArray browserFolders;
    int targetOsc = 1;
    juce::TimeSliceThread scanThread { "Waveform scanner" };
    juce::WildcardFileFilter waveFilter { "*.wav;*.aif;*.aiff;*.flac", "*", "Waveforms" };
    juce::DirectoryContentsList folderContents { &waveFilter, scanThread };
    juce::FileTreeComponent folderTree { folderContents };
    juce::TextButton targetOsc1Button { "Osc 1" }, targetOsc2Button { "Osc 2" };
    juce::TextButton browserToggleButton { "HIDE" };
    bool browserVisible = true;
    juce::ComboBox folderBox;
    juce::TextButton addFolderButton { "Add Folder..." }, removeFolderButton { "Remove" };
    juce::Label hintLabel, status1Label, status2Label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableSynthAudioProcessorEditor)
};
