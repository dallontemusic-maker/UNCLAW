#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "UnclawLookAndFeel.h"
#include <functional>
#include <vector>

// Сегментный переключатель для параметра-списка: одна кнопка на каждый пункт.
class ChoiceButtons : public juce::Component {
public:
    ChoiceButtons(juce::RangedAudioParameter& parameter, const juce::StringArray& labels)
        : attachment(parameter, [this] (float value) { select((int) std::lround(value)); }, nullptr) {
        for (int i = 0; i < labels.size(); ++i) {
            auto* b = buttons.add(new juce::TextButton(labels[i]));
            b->setClickingTogglesState(false);
            b->setConnectedEdges((i > 0 ? juce::Button::ConnectedOnLeft : 0)
                                 | (i < labels.size() - 1 ? juce::Button::ConnectedOnRight : 0));
            b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1d2023));
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff684e2a));
            b->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffaaa9a2));
            b->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffdc86));
            b->onClick = [this, i] { attachment.setValueAsCompleteGesture((float) i); };
            addAndMakeVisible(b);
        }
        attachment.sendInitialUpdate();
    }

    int getSelected() const { return selected; }
    std::function<void(int)> onSelectionChanged;

    void resized() override {
        auto r = getLocalBounds();
        const int n = buttons.size();
        for (int i = 0; i < n; ++i)
            buttons[i]->setBounds(r.removeFromLeft(i == n - 1 ? r.getWidth() : r.getWidth() / (n - i)));
    }

private:
    void select(int index) {
        selected = juce::jlimit(0, juce::jmax(0, buttons.size() - 1), index);
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState(i == selected, juce::dontSendNotification);
        if (onSelectionChanged) onSelectionChanged(selected);
    }

    juce::OwnedArray<juce::TextButton> buttons;
    int selected = 0;
    juce::ParameterAttachment attachment;   // объявлен после кнопок: колбэк обращается к ним
};

// Экран сэмпла: форма волны, точка старта, зона петли и подсказка для перетаскивания файла.
class SampleDisplay : public juce::Component {
public:
    struct State {
        std::vector<float> peaks;
        juce::String name, status;
        double seconds = 0.0;
        float start = 0.0f;
        bool loop = false, reverse = false;
    };

    void setState(State s) { state = std::move(s); repaint(); }
    void setDragHover(bool hover) { if (hover != dragHover) { dragHover = hover; repaint(); } }
    std::function<void()> onClick;

    void mouseUp(const juce::MouseEvent& e) override {
        if (onClick && e.mouseWasClicked()) onClick();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat();
        const auto accent = unclaw::accentOf(*this);
        unclaw::drawScreen(g, area, 8, 2, 4.0f);

        if (dragHover || state.peaks.empty()) {
            if (dragHover) {
                g.setColour(accent.withAlpha(0.10f));
                g.fillRoundedRectangle(area, 3.0f);
            }
            juce::Path outline;
            outline.addRoundedRectangle(area.reduced(1.0f), 3.0f);
            juce::Path dashed;
            const float dashes[] { 4.0f, 3.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
            g.setColour(dragHover ? accent : juce::Colour(0xff5b5e61));
            g.fillPath(dashed);

            g.setColour(dragHover ? accent : unclaw::colours::text);
            g.setFont(unclaw::uiFont(9.0f, true, 0.12f));
            const auto text = getLocalBounds().withTrimmedBottom(14).withTrimmedTop(4);   // низ занят LOOP / REV / KEY
            g.drawText("DROP SAMPLE", text.withHeight(text.getHeight() / 2), juce::Justification::centredBottom);
            g.setColour(unclaw::colours::muted);
            g.setFont(unclaw::uiFont(7.5f, false));
            const auto hint = state.status.isNotEmpty() && !dragHover ? state.status : juce::String("or click to browse");
            g.drawFittedText(hint, text.withTrimmedTop(text.getHeight() / 2 + 2).reduced(4, 0),
                             juce::Justification::centredTop, 1);
            return;
        }

        const auto wave = area.reduced(3.0f).withTrimmedTop(12.0f).withTrimmedBottom(13.0f);
        const float startX = wave.getX() + wave.getWidth() * (state.reverse ? 1.0f - state.start : state.start);
        if (state.loop) {   // петля: от точки старта до конца файла (в обратном режиме — до начала)
            const auto loopArea = state.reverse ? wave.withRight(startX) : wave.withLeft(startX);
            g.setColour(accent.withAlpha(0.10f));
            g.fillRect(loopArea);
        }

        const int n = (int) state.peaks.size();
        g.setColour(accent.withAlpha(0.85f));
        for (int x = 0; x < (int) wave.getWidth(); ++x) {
            const float p = state.peaks[(size_t) juce::jmin(n - 1, x * n / juce::jmax(1, (int) wave.getWidth()))];
            const float h = juce::jmax(0.5f, p * wave.getHeight() * 0.5f);
            g.fillRect(wave.getX() + (float) x, wave.getCentreY() - h, 1.0f, h * 2.0f);
        }

        // всё, что не прозвучит, затемняем
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillRect(state.reverse ? wave.withLeft(startX) : wave.withRight(startX));
        g.setColour(juce::Colours::white);
        g.drawLine(startX, wave.getY() - 1.0f, startX, wave.getBottom() + 1.0f, 1.2f);

        g.setColour(unclaw::colours::text);
        g.setFont(unclaw::uiFont(8.0f));
        g.drawText(state.name + "  " + juce::String(state.seconds, 2) + " s",
                   getLocalBounds().reduced(4, 1).withHeight(12).withTrimmedRight(56),
                   juce::Justification::centredLeft, true);
    }

private:
    State state;
    bool dragHover = false;
};
