#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <initializer_list>

// Стиль UNCLAW v2: монохромный графит и один акцент. Каждому источнику — свой цвет акцента
// (OSC 1 зелёный, OSC 2 янтарный, гибридный — голубой); он задаётся свойством компонента или его родителя.
namespace unclaw {

namespace colours {
    const juce::Colour amber   { 0xffffc21a };
    const juce::Colour green   { 0xffa6e35b };
    const juce::Colour cyan    { 0xff5ccbff };
    const juce::Colour hot     { 0xffff7a2f };
    const juce::Colour text    { 0xffece9e1 };
    const juce::Colour muted   { 0xff8b8983 };
    const juce::Colour value   { 0xffd9d6cc };
    const juce::Colour line    { 0xff26282b };
    const juce::Colour outline { 0xff2b2d30 };
    const juce::Colour field   { 0xff0d0e0f };
    const juce::Colour button  { 0xff141517 };
    const juce::Colour screen  { 0xff08090a };
    const juce::Colour panelTop    { 0xff1a1b1e };
    const juce::Colour panelBottom { 0xff121315 };
}

inline const juce::Identifier accentProperty { "unclawAccent" };
inline const juce::Identifier bipolarProperty { "unclawBipolar" };

inline void setAccent(std::initializer_list<juce::Component*> components, juce::Colour colour) {
    for (auto* c : components) c->getProperties().set(accentProperty, (juce::int64) colour.getARGB());
}

inline juce::Colour accentOf(const juce::Component& component) {
    for (auto* c = &component; c != nullptr; c = c->getParentComponent())
        if (auto* v = c->getProperties().getVarPointer(accentProperty))
            return juce::Colour((juce::uint32) (juce::int64) *v);
    return colours::amber;
}

inline juce::Font uiFont(float height, bool bold = true, float kerning = 0.0f) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor(kerning);
}

inline juce::Font monoFont(float height) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

// Свечение без размытия: та же линия несколько раз — шире и прозрачнее, сверху яркое ядро.
inline void strokeGlow(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width,
                       std::initializer_list<std::pair<float, float>> layers = { { 3.4f, 0.10f }, { 2.0f, 0.22f } },
                       juce::Colour core = {}) {
    for (auto [scale, alpha] : layers) {
        g.setColour(colour.withAlpha(alpha));
        g.strokePath(path, juce::PathStrokeType(width * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(core.isTransparent() ? colour : core);
    g.strokePath(path, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Утопленный экран: тёмный фон, внутренняя тень и едва заметная сетка
inline void drawScreen(juce::Graphics& g, juce::Rectangle<float> r, int gridX, int gridY, float radius = 4.0f) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0a0b0c), r.getX(), r.getY(),
                                           juce::Colour(0xff060707), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(juce::Colours::white.withAlpha(0.045f));
    for (int i = 1; i < gridX; ++i) {
        const float x = r.getX() + r.getWidth() * (float) i / (float) gridX;
        g.drawLine(x, r.getY() + 2.0f, x, r.getBottom() - 2.0f, 1.0f);
    }
    for (int i = 1; i < gridY; ++i) {
        const float y = r.getY() + r.getHeight() * (float) i / (float) gridY;
        g.drawLine(r.getX() + 2.0f, y, r.getRight() - 2.0f, y, 1.0f);
    }
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
    g.setColour(juce::Colour(0xff1d1f22));
    g.drawRoundedRectangle(r, radius, 1.0f);
}

inline void drawLed(juce::Graphics& g, juce::Point<float> c, float r, bool on, juce::Colour colour) {
    if (on) {
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.55f), c, colour.withAlpha(0.0f), c.translated(r * 3.0f, 0.0f), true));
        g.fillEllipse(juce::Rectangle<float>(r * 6.0f, r * 6.0f).withCentre(c));
        g.setColour(colour);
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.fillEllipse(juce::Rectangle<float>(r * 0.8f, r * 0.8f).withCentre(c.translated(-r * 0.3f, -r * 0.3f)));
    } else {
        g.setColour(juce::Colour(0xff2c2e31));
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));
        g.setColour(juce::Colours::black);
        g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), 0.6f);
    }
}

class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    LookAndFeel() {
        setColour(juce::PopupMenu::backgroundColourId, colours::button);
        setColour(juce::PopupMenu::textColourId, colours::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::amber.withAlpha(0.25f));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::TooltipWindow::backgroundColourId, colours::button);
        setColour(juce::TooltipWindow::textColourId, colours::text);
        setColour(juce::TooltipWindow::outlineColourId, colours::outline);
        setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff3a3d41));
        setColour(juce::Label::textColourId, colours::text);
    }

    // ---- ручки ---------------------------------------------------------------------------------
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float pos,
                          float startAngle, float endAngle, juce::Slider& slider) override {
        const auto colour = accentOf(slider);
        const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
        const float rad = juce::jmax(5.0f, juce::jmin(area.getWidth(), area.getHeight()) / 2.0f - 0.5f);
        const float lw = juce::jlimit(1.4f, 3.0f, rad * 0.14f);
        const bool tick = rad >= 11.0f;   // у мелких ручек без риски шкала крупнее
        const float ring = rad - lw / 2.0f - (tick ? 1.6f : 0.2f);
        const float gap = juce::jlimit(0.9f, 3.5f, rad * 0.12f);
        const float body = juce::jmax(2.5f, ring - lw / 2.0f - gap);
        const auto c = area.getCentre().translated(0.0f, 0.5f);
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool bipolar = slider.getProperties()[bipolarProperty];
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (tick) {
            g.setColour(juce::Colour(0xff6d6b66));
            g.fillEllipse(juce::Rectangle<float>(1.6f, 1.6f).withCentre({ c.x, c.y - rad + 0.6f }));
        }
        juce::Path track;
        track.addCentredArc(c.x, c.y, ring, ring, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff25272a));
        g.strokePath(track, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (std::abs(angle - from) > 0.02f) {
            juce::Path value;
            value.addCentredArc(c.x, c.y, ring, ring, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
            if (slider.isEnabled() && slider.getAlpha() > 0.5f) {
                if (rad > 9.0f) strokeGlow(g, value, colour, lw);
                else strokeGlow(g, value, colour, lw, { { 2.2f, 0.18f } });
            } else {
                g.setColour(colour.withAlpha(0.5f));
                g.strokePath(value, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        const auto bodyRect = juce::Rectangle<float>(body * 2.0f, body * 2.0f).withCentre(c);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.fillEllipse(bodyRect.translated(0.6f, 1.2f));
        juce::ColourGradient bodyFill(juce::Colour(0xff3b3e42), c.x - body * 0.24f, c.y - body * 0.4f,
                                      juce::Colour(0xff0d0e0f), c.x + body * 0.7f, c.y + body * 0.8f, true);
        bodyFill.addColour(0.55, juce::Colour(0xff1c1e20));
        g.setGradientFill(bodyFill);
        g.fillEllipse(bodyRect);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.28f), c.x, c.y - body,
                                               juce::Colours::black.withAlpha(0.4f), c.x, c.y + body, false));
        g.drawEllipse(bodyRect.reduced(0.4f), 0.8f);
        if (body > 7.0f) {
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a2c2f), c.x - body * 0.15f, c.y - body * 0.2f,
                                                   juce::Colour(0xff151618), c.x + body * 0.5f, c.y + body * 0.5f, true));
            g.fillEllipse(juce::Rectangle<float>(body * 1.24f, body * 1.24f).withCentre(c));
        }
        const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
        g.setColour(juce::Colour(0xfff3f0e7));
        g.drawLine(juce::Line<float>(c + dir * (body * 0.30f), c + dir * (body * 0.86f)), juce::jlimit(1.0f, 2.0f, body * 0.14f));
    }

    // ---- колёса pitch / mod: рифлёный цилиндр с подсвеченной меткой ------------------------------
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float minPos,
                          float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override {
        if (style != juce::Slider::LinearVertical) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }
        juce::ignoreUnused(x, y, width, height, sliderPos);
        // всё поле слайдера над подписью, а не дорожка за вычетом радиуса бегунка
        const auto wheel = getSliderLayout(slider).sliderBounds.toFloat().reduced(3.0f, 3.0f);
        g.setColour(juce::Colour(0xff060707));
        g.fillRoundedRectangle(wheel.expanded(2.0f), 5.0f);
        g.setColour(juce::Colour(0xff222427));
        g.drawRoundedRectangle(wheel.expanded(2.0f), 5.0f, 1.0f);
        juce::ColourGradient cylinder(juce::Colour(0xff0c0d0e), wheel.getX(), 0.0f, juce::Colour(0xff0c0d0e), wheel.getRight(), 0.0f, false);
        cylinder.addColour(0.45, juce::Colour(0xff3a3d41));
        cylinder.addColour(0.55, juce::Colour(0xff34373b));
        g.setGradientFill(cylinder);
        g.fillRoundedRectangle(wheel, 3.5f);
        constexpr int ribs = 16;
        for (int k = 1; k < ribs; ++k) {
            const float t = (float) k / ribs;
            g.setColour(juce::Colours::black.withAlpha(0.25f + 0.35f * std::sin(juce::MathConstants<float>::pi * t)));
            const float ry = wheel.getY() + wheel.getHeight() * t;
            g.drawLine(wheel.getX() + 1.5f, ry, wheel.getRight() - 1.5f, ry, 1.0f);
        }
        const auto proportion = (float) slider.valueToProportionOfLength(slider.getValue());
        const float my = juce::jlimit(wheel.getY() + 3.0f, wheel.getBottom() - 3.0f,
                                      wheel.getBottom() - 3.0f - proportion * (wheel.getHeight() - 6.0f));
        juce::Path marker;
        marker.startNewSubPath(wheel.getX() + 2.0f, my);
        marker.lineTo(wheel.getRight() - 2.0f, my);
        strokeGlow(g, marker, accentOf(slider), 1.6f, { { 3.0f, 0.2f } });
    }

    int getSliderThumbRadius(juce::Slider& slider) override {
        // у колёс нет круглого бегунка: отступ почти не нужен, цилиндр занимает всю высоту
        return slider.getSliderStyle() == juce::Slider::LinearVertical ? 3 : LookAndFeel_V4::getSliderThumbRadius(slider);
    }

    // ---- поле значения под ручкой: «таблетка» с моноширинными цифрами ---------------------------
    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setColour(juce::Label::textColourId, colours::value);
        label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        return label;
    }

    juce::Font getLabelFont(juce::Label& label) override {
        if (dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr)
            return monoFont(juce::jlimit(6.5f, 9.5f, (float) label.getHeight() - 3.5f));
        return LookAndFeel_V4::getLabelFont(label);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override {
        if (dynamic_cast<juce::Slider*>(label.getParentComponent()) == nullptr || label.isBeingEdited()) {
            LookAndFeel_V4::drawLabel(g, label);
            return;
        }
        const auto r = label.getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(juce::Colour(0xff0b0c0d));
        g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
        g.setColour(juce::Colour(0xff222427));
        g.drawRoundedRectangle(r, r.getHeight() / 2.0f, 1.0f);
        g.setColour(label.findColour(juce::Label::textColourId));
        g.setFont(getLabelFont(label));
        g.drawFittedText(label.getText(), label.getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.7f);
    }

    // ---- переключатели-светодиоды ----------------------------------------------------------------
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override {
        const auto colour = accentOf(button);
        const bool on = button.getToggleState();
        const auto r = button.getLocalBounds().toFloat();
        const float led = juce::jlimit(2.2f, 3.4f, r.getHeight() * 0.2f);
        const juce::Point<float> c(r.getX() + 3.0f + led, r.getCentreY());
        drawLed(g, c, led, on && button.isEnabled(), colour);
        g.setColour((on ? colours::text : (highlighted ? colours::value : colours::muted))
                        .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.setFont(uiFont(juce::jlimit(6.5f, 9.5f, r.getHeight() * 0.5f), true, 0.03f));
        const auto textArea = r.withTrimmedLeft(c.x + led + 4.0f - r.getX()).toNearestInt();
        g.drawFittedText(button.getButtonText(), textArea, juce::Justification::centredLeft, 2, 0.75f);
    }

    // ---- кнопки: выключенная — графит, включённая — заливка акцентом ------------------------------
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool down) override {
        const auto colour = accentOf(button);
        const auto r = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        const float cs = 4.0f;
        const bool flatL = button.isConnectedOnLeft(), flatR = button.isConnectedOnRight();
        juce::Path shape;
        shape.addRoundedRectangle(r.getX(), r.getY(), r.getWidth(), r.getHeight(), cs, cs, !flatL, !flatR, !flatL, !flatR);
        if (on) {
            g.setColour(colour.withAlpha(0.14f));
            g.fillPath(shape);
            juce::Path inner;
            const auto ir = r.reduced(1.0f);
            inner.addRoundedRectangle(ir.getX(), ir.getY(), ir.getWidth(), ir.getHeight(), cs - 1.0f, cs - 1.0f, !flatL, !flatR, !flatL, !flatR);
            g.setColour(down ? colour.darker(0.2f) : colour);
            g.fillPath(inner);
        } else {
            g.setColour(down ? juce::Colour(0xff0f1012) : (highlighted ? juce::Colour(0xff1c1d20) : colours::button));
            g.fillPath(shape);
            g.setColour(colours::outline);
            g.strokePath(shape, juce::PathStrokeType(1.0f));
        }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
        return uiFont(juce::jlimit(7.0f, 10.5f, (float) buttonHeight * 0.46f), true, 0.04f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool highlighted, bool) override {
        const bool on = button.getToggleState();
        g.setColour(on ? juce::Colour(0xff141414) : (highlighted ? colours::text : juce::Colour(0xffbdbab2)));
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(3, 1), juce::Justification::centred, 1, 0.75f);
    }

    // ---- списки ----------------------------------------------------------------------------------
    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override {
        const auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
        g.setColour(colours::field);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(box.hasKeyboardFocus(true) ? accentOf(box).withAlpha(0.6f) : colours::outline);
        g.drawRoundedRectangle(r, 4.0f, 1.0f);
        const float ax = (float) width - arrowZone(width, height) * 0.5f - 1.0f, ay = (float) height * 0.5f;
        const float aw = juce::jlimit(2.0f, 3.0f, (float) height * 0.14f);
        juce::Path arrow;
        arrow.startNewSubPath(ax - aw, ay - aw * 0.5f);
        arrow.lineTo(ax, ay + aw * 0.55f);
        arrow.lineTo(ax + aw, ay - aw * 0.5f);
        g.setColour(colours::muted);
        g.strokePath(arrow, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox& box) override {
        return uiFont(juce::jlimit(7.5f, 11.0f, (float) box.getHeight() * 0.5f), true);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(4, 1, box.getWidth() - 4 - (int) arrowZone(box.getWidth(), box.getHeight()), box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setColour(juce::Label::textColourId, colours::text);
        label.setMinimumHorizontalScale(0.75f);
    }

private:
    // в узких списках стрелка занимает меньше места, чтобы текст («Poly») помещался
    static float arrowZone(int width, int height) { return juce::jmin(juce::jlimit(11.0f, 18.0f, (float) height * 0.8f), (float) width * 0.3f); }
};

// Клавиатура: белые клавиши с объёмом, нажатые подсвечиваются акцентом
class Keyboard : public juce::MidiKeyboardComponent {
public:
    Keyboard(juce::MidiKeyboardState& state, Orientation orientation) : MidiKeyboardComponent(state, orientation) {
        setColour(whiteNoteColourId, juce::Colour(0xff050606));   // фон между клавишами
        setColour(shadowColourId, juce::Colours::black.withAlpha(0.55f));
        setColour(keySeparatorLineColourId, juce::Colours::transparentBlack);
        setColour(upDownButtonBackgroundColourId, colours::button);
    }

    void drawWhiteNote(int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                       juce::Colour, juce::Colour) override {
        const auto key = area.reduced(0.5f, 0.0f);
        juce::ColourGradient fill(juce::Colour(0xffe9e5da), key.getX(), key.getY(), juce::Colour(0xffcfcabe), key.getX(), key.getBottom(), false);
        fill.addColour(0.85, juce::Colour(0xfff6f3ea));
        g.setGradientFill(fill);
        g.fillRoundedRectangle(key, 2.0f);
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRoundedRectangle(key.withTop(key.getBottom() - 4.0f), 2.0f);
        if (isDown) {
            g.setGradientFill(juce::ColourGradient(colours::amber.withAlpha(0.25f), key.getX(), key.getY(),
                                                   colours::amber.withAlpha(0.85f), key.getX(), key.getBottom(), false));
            g.fillRoundedRectangle(key, 2.0f);
        } else if (isOver) {
            g.setColour(colours::amber.withAlpha(0.15f));
            g.fillRoundedRectangle(key, 2.0f);
        }
        if (note % 12 == 0) {
            g.setColour(juce::Colour(0xff8a867c));
            g.setFont(uiFont(juce::jlimit(6.0f, 8.0f, key.getWidth() * 0.32f)));
            g.drawText(juce::MidiMessage::getMidiNoteName(note, true, true, 3), key.withTop(key.getBottom() - 14.0f),
                       juce::Justification::centred);
        }
    }

    void drawBlackNote(int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour) override {
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(area.translated(1.0f, 0.0f).withTrimmedBottom(-2.0f), 2.0f);
        juce::ColourGradient fill = isDown
            ? juce::ColourGradient(juce::Colour(0xff4a3a08), area.getX(), area.getY(), colours::amber, area.getX(), area.getBottom(), false)
            : juce::ColourGradient(juce::Colour(0xff1d1f22), area.getX(), area.getY(), juce::Colour(0xff2a2c2f), area.getX(), area.getBottom(), false);
        if (!isDown) fill.addColour(0.8, juce::Colour(0xff0b0c0d));
        g.setGradientFill(fill);
        g.fillRoundedRectangle(area, 2.0f);
        g.setColour(juce::Colours::white.withAlpha(isDown ? 0.25f : (isOver ? 0.14f : 0.07f)));
        g.fillRoundedRectangle(area.reduced(1.5f, 0.0f).withTop(area.getBottom() - 5.0f).withHeight(3.5f), 1.2f);
    }
};

} // namespace unclaw
