#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "OscilloscopeDisplay.h"
#include "BrowserSettings.h"
#include "HybridComponents.h"
#include "UnclawLookAndFeel.h"
#include <array>
#include <vector>

class WavetableSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                           public juce::DragAndDropContainer,
                                           public juce::DragAndDropTarget,
                                           public juce::FileDragAndDropTarget,
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
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;
    // Файлы из Finder / Explorer
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;
    void refreshFolderBox(int selectIndex);
    void showFolder(int index);
    void loadFromBrowser(const juce::File& file);
    void loadExternalWavetable(const juce::File& file, int oscillator);
    void loadHybridSample(const juce::File& file);
    void loadIntoTarget(const juce::File& file, int target);   // 1, 2 — осцилляторы, 3 — гибридный источник
    int dropTargetAt(juce::Point<int> point) const;
    void setDragHover(int target);
    void layoutHybrid(juce::Rectangle<int> card);
    void paintHybridPreview(juce::Graphics& g);
    void updateSampleDisplay();

    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttachment = APVTS::SliderAttachment;
    using ButtonAttachment = APVTS::ButtonAttachment;
    using ComboAttachment = APVTS::ComboBoxAttachment;

    enum class CaptionKind { Section, Knob, Side };
    struct Caption { juce::Rectangle<int> bounds; juce::String text; CaptionKind kind; };
    struct Panel { juce::Rectangle<int> bounds; juce::Colour colour; };
    std::vector<Caption> captions;
    std::vector<Panel> panels;
    std::array<juce::Rectangle<int>, 3> oscillatorDropBounds;   // OSC 1, OSC 2, HYBRID
    int dragHoverOscillator = 0;
    bool dragStartedFromBrowser = false;

    void placeKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell);
    void placeCompactKnob(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> cell);
    void placeCombo(juce::ComboBox& box, const juce::String& name, juce::Rectangle<int> cell);
    void placeHFader(juce::Slider& slider, const juce::String& name, juce::Rectangle<int> row);
    void addSection(const juce::String& name, juce::Rectangle<int> bounds);

    WavetableSynthAudioProcessor& audioProcessor;
    unclaw::LookAndFeel lookAndFeel;   // объявлен до компонентов: должен пережить их
    juce::TooltipWindow tooltipWindow { this, 700 };
    OscilloscopeDisplay oscilloscope;
    unclaw::Keyboard keyboard;

    juce::TextButton optionsButton { "OPTIONS" };
    juce::TextButton loadOsc1Button { "Load WAV..." };
    juce::TextButton loadOsc2Button { "Load WAV..." };
    juce::TextButton synthTabButton { "SYNTH" }, effectsTabButton { "EFFECTS" };   // арпеджиатор — в ряду с LFO
    int currentPage = 0;

    // Панель осциллятора: форма, позиция в таблице, громкость, высота, фаза, панорама, юнисон
    struct OscUI {
        juce::ComboBox wave;
        juce::Slider wtPos, level, oct, semi, fine, phase, pan, unison, detune, stereo;
        juce::Slider attack, decay, sustain, release;
        juce::TextButton analogButton { "ANALOG MODE" };
        juce::ToggleButton enabled { "ON" };
        juce::ToggleButton toFilter { "TO FILTER" };
    };
    std::array<OscUI, 2> oscUI;
    std::array<juce::Rectangle<int>, 2> wavePreviewBounds;

    // Гибридный источник: SUB / NOISE / SAMPLE (параметры SUB_*, NOISE_*, SMP_*)
    std::unique_ptr<ChoiceButtons> hybridModeButtons, subShapeButtons, noiseColourButtons, hybridRouteButtons;
    juce::Slider subLevelSlider, subOctaveSlider, hybridFineSlider, hybridPanSlider;
    juce::Slider noiseTiltSlider, noiseStereoSlider, samplePitchSlider, sampleStartSlider;
    juce::Slider subAttackSlider, subDecaySlider, subSustainSlider, subReleaseSlider;
    juce::ToggleButton subEnabledButton { "ON" };
    juce::ToggleButton sampleLoopButton { "LOOP" }, sampleReverseButton { "REV" }, sampleKeytrackButton { "KEY" };
    juce::ComboBox sampleRootBox;
    SampleDisplay sampleDisplay;
    juce::Rectangle<int> hybridPreviewBounds;
    juce::String lastSampleKey;
    juce::Slider cutoffSlider, resonanceSlider;
    juce::ToggleButton filterOnButton { "FILTER ON" };
    std::unique_ptr<ChoiceButtons> filterTypeButtons;
    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;   // горизонтальные фейдеры
    juce::Slider filterEnvAttackSlider, filterEnvDecaySlider, filterEnvSustainSlider,
                  filterEnvReleaseSlider, filterEnvAmountSlider;
    juce::Slider masterVolumeSlider, masterPanSlider;
    juce::Rectangle<int> outputMeterBounds;
    std::array<float, 2> meterHold {};       // удержание пика индикатора (доля шкалы)
    std::array<int, 2> meterHoldTicks {};
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
    juce::ToggleButton arpEnableButton { "ON" };
    juce::ComboBox arpModeBox, arpRateBox;
    juce::Slider arpOctSlider, arpGateSlider;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::FileChooser> folderChooser;
    std::unique_ptr<juce::FileChooser> sampleChooser;

    // Браузер вейвтейблов (слева). Порядок объявления важен: поток сканирования должен пережить список и дерево.
    BrowserSettings browserSettings;
    juce::StringArray browserFolders;
    int targetOsc = 1;
    juce::TimeSliceThread scanThread { "Waveform scanner" };
    juce::WildcardFileFilter waveFilter { "*.wav;*.aif;*.aiff;*.flac", "*", "Waveforms" };
    juce::DirectoryContentsList folderContents { &waveFilter, scanThread };
    juce::FileTreeComponent folderTree { folderContents };
    juce::TextButton targetOsc1Button { "Osc 1" }, targetOsc2Button { "Osc 2" }, targetSampleButton { "Smp" };
    juce::TextButton browserToggleButton { "HIDE" };
    bool browserVisible = true;
    juce::ComboBox folderBox;
    juce::TextButton addFolderButton { "Add Folder..." }, removeFolderButton { "Remove" };
    juce::Label hintLabel, status1Label, status2Label;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableSynthAudioProcessorEditor)
};
