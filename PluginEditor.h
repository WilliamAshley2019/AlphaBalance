#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SquareButton : public juce::Component
{
public:
    SquareButton(juce::String buttonText) : text(buttonText) {}

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        bool isOver = isMouseOverOrDragging();

        g.setColour(juce::Colour(15, 15, 15));
        g.fillRect(bounds);

        juce::Colour topCol = isOver ? juce::Colour(60, 60, 60) : juce::Colour(45, 45, 45);
        juce::Colour botCol = isOver ? juce::Colour(20, 20, 20) : juce::Colour(10, 10, 10);

        juce::ColourGradient grad(topCol, bounds.getCentreX(), bounds.getY(),
            botCol, bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRect(bounds.reduced(2.0f));

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
        g.drawText(text, getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    std::function<void()> onClick;

private:
    juce::String text;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SquareButton)
};

class SmallLcdDisplay final : public juce::Component
{
public:
    SmallLcdDisplay(int w = 100, int h = 40) { setSize(w, h); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(120, 140, 100));
        g.setColour(juce::Colour(60, 70, 50));
        g.drawRect(getLocalBounds(), 2);
        g.setColour(juce::Colour(20, 25, 15));

        const int pad = 2;
        const int labelH = 14;
        const int barH = 3;
        const int barPad = 4;

        g.setFont(juce::Font(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold)));
        g.drawText(label, pad, pad, getWidth() - 2 * pad, labelH,
            juce::Justification::centred, false);

        const int valueTop = pad + labelH + 1;
        const int valueH = getHeight() - valueTop - pad - (barEnabled ? barH + barPad : 0);

        g.setFont(juce::Font(juce::FontOptions(
            juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain)));
        g.drawFittedText(valueText, pad, valueTop, getWidth() - 2 * pad, valueH,
            juce::Justification::centred, 1, 0.8f);

        if (barEnabled)
        {
            const int x0 = 4;
            const int y0 = getHeight() - barPad - barH;
            const int w = getWidth() - 8;
            g.setColour(juce::Colour(20, 25, 15).withAlpha(0.45f));
            g.fillRect(x0, y0, w, barH);
            g.setColour(juce::Colour(20, 25, 15));
            g.fillRect(x0, y0, (int)(w * juce::jlimit(0.0f, 1.0f, barValue)), barH);
        }
    }

    void setLabel(const juce::String& t) { if (label != t) { label = t; repaint(); } }
    void setValue(const juce::String& t) { if (valueText != t) { valueText = t; repaint(); } }
    void setBar(float v01)
    {
        if (!barEnabled) barEnabled = true;
        if (barValue != v01) { barValue = v01; repaint(); }
    }

    std::function<void(const juce::MouseEvent&)> onMouseDown;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onMouseDown) onMouseDown(e);
    }

private:
    juce::String label, valueText;
    float barValue = 0.0f;
    bool barEnabled = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmallLcdDisplay)
};

class BlackMetalKnobLNF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
        float sliderPos, float rotaryStartAngle,
        float rotaryEndAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float>(
            (float)x, (float)y, (float)w, (float)h);

        const float side = juce::jmin(bounds.getWidth(), bounds.getHeight());
        bounds = bounds.withSizeKeepingCentre(side, side).reduced(6.0f);

        const float radius = bounds.getWidth() * 0.5f;
        const float angle = rotaryStartAngle
            + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        g.setColour(juce::Colour(15, 15, 15));
        g.fillEllipse(bounds);

        juce::ColourGradient grad(
            juce::Colour(45, 45, 45), bounds.getCentreX(), bounds.getY(),
            juce::Colour(10, 10, 10), bounds.getCentreX(), bounds.getBottom(), false);
        g.setGradientFill(grad);
        g.fillEllipse(bounds.reduced(radius * 0.15f));

        juce::Path p;
        p.addRectangle(-1.5f, -radius * 0.6f, 3.0f, radius * 0.4f);
        g.setColour(juce::Colours::white);
        g.fillPath(p, juce::AffineTransform::rotation(angle)
            .translated(bounds.getCentre()));
    }
};

class AlphaDelayEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AlphaDelayEditor(AlphaDelayProcessor&);
    ~AlphaDelayEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void changePreset(int direction);
    void onSaveClicked();
    void updateReadouts();

    void showEditorFor(int which);
    void commitEdit(int which);
    void hideEditors();

    AlphaDelayProcessor& proc;
    BlackMetalKnobLNF lnf;

    juce::Slider balanceKnob, volumeKnob;
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> balanceAtt, volumeAtt;

    juce::Label titleLabel;
    juce::Label balanceLabel, volumeLabel;

    SmallLcdDisplay presetLcd;
    SmallLcdDisplay balanceLcd;

    SquareButton prevButton{ "<" };
    SquareButton nextButton{ ">" };
    SquareButton saveButton{ "S" };

    std::unique_ptr<juce::FileChooser> chooser;

    juce::String lastPresetText, lastBalanceText;

    juce::TextEditor balanceEdit, volumeEdit;
    int activeEdit = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AlphaDelayEditor)
};