#include "PluginEditor.h"

AlphaDelayEditor::AlphaDelayEditor(AlphaDelayProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setSize(440, 340);
    setResizable(false, false);

    auto setupKnob = [this](juce::Slider& k, const juce::String& id,
        std::unique_ptr<Attachment>& att)
        {
            k.setLookAndFeel(&lnf);
            k.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            k.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible(k);
            att = std::make_unique<Attachment>(proc.apvts, id, k);
        };

    setupKnob(balanceKnob, "balance", balanceAtt);
    setupKnob(volumeKnob, "volume", volumeAtt);

    titleLabel.setText("ALPHA BALANCE", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colour(220, 220, 220));
    addAndMakeVisible(titleLabel);

    auto knobLabel = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::Font(juce::FontOptions(11.0f)));
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colour(180, 180, 185));
        addAndMakeVisible(l);
        };
    knobLabel(balanceLabel, "BALANCE");
    knobLabel(volumeLabel, "VOLUME");

    presetLcd.setLabel("PRESET");
    addAndMakeVisible(presetLcd);

    balanceLcd.setLabel("BAL / VOL");
    addAndMakeVisible(balanceLcd);

    balanceLcd.onMouseDown = [this](const juce::MouseEvent& e) {
        if (activeEdit != 0) return;
        const int midX = balanceLcd.getWidth() / 2;
        showEditorFor(e.x < midX ? 1 : 2);
        };

    prevButton.onClick = [this] { changePreset(-1); };
    addAndMakeVisible(prevButton);
    nextButton.onClick = [this] { changePreset(1); };
    addAndMakeVisible(nextButton);
    saveButton.onClick = [this] { onSaveClicked(); };
    addAndMakeVisible(saveButton);

    auto setupEditor = [this](juce::TextEditor& e) {
        e.setMultiLine(false);
        e.setReturnKeyStartsNewLine(false);
        e.setJustification(juce::Justification::centred);
        e.setFont(juce::Font(juce::FontOptions(15.0f)));
        e.setColour(juce::TextEditor::backgroundColourId, juce::Colour(120, 140, 100));
        e.setColour(juce::TextEditor::textColourId, juce::Colour(20, 25, 15));
        e.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        e.setColour(juce::TextEditor::highlightColourId, juce::Colour(60, 70, 50).withAlpha(0.4f));
        e.setColour(juce::CaretComponent::caretColourId, juce::Colour(20, 25, 15));
        e.setVisible(false);
        addChildComponent(e);
        };

    setupEditor(balanceEdit);
    setupEditor(volumeEdit);

    balanceEdit.onReturnKey = [this] { commitEdit(1); };
    volumeEdit.onReturnKey = [this] { commitEdit(2); };
    balanceEdit.onEscapeKey = [this] { hideEditors(); };
    volumeEdit.onEscapeKey = [this] { hideEditors(); };
    balanceEdit.onFocusLost = [this] { if (activeEdit == 1) commitEdit(1); };
    volumeEdit.onFocusLost = [this] { if (activeEdit == 2) commitEdit(2); };

    startTimerHz(30);
    updateReadouts();
}

AlphaDelayEditor::~AlphaDelayEditor()
{
    stopTimer();
    balanceAtt.reset();
    volumeAtt.reset();
    balanceKnob.setLookAndFeel(nullptr);
    volumeKnob.setLookAndFeel(nullptr);
}

void AlphaDelayEditor::changePreset(int direction)
{
    const int current = proc.getCurrentProgram();
    const int numPrograms = proc.getNumPrograms();
    if (numPrograms == 0) return;
    int next = current + direction;
    if (next < 0) next = numPrograms - 1;
    if (next >= numPrograms) next = 0;
    proc.setCurrentProgram(next);
}

void AlphaDelayEditor::onSaveClicked()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Save preset as...",
        proc.presets.presetFolder().getChildFile("My Preset.alph"),
        "*.alph");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode
        | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File{}) return;
            if (f.getFileExtension().isEmpty())
                f = f.withFileExtension(".alph");
            juce::String err;
            if (!proc.saveCurrentAsUserPreset(f.getFileNameWithoutExtension(), err))
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Save Preset Failed", err);
                return;
            }
            timerCallback();
        });
}

void AlphaDelayEditor::timerCallback()
{
    updateReadouts();
}

void AlphaDelayEditor::updateReadouts()
{
    const int current = proc.getCurrentProgram();
    const auto names = proc.presets.allProgramNames();
    if (current >= 0 && current < names.size())
    {
        const juce::String t = names[current];
        if (t != lastPresetText) { presetLcd.setValue(t); lastPresetText = t; }
    }

    if (activeEdit != 0) return;

    const int b = static_cast<int>(balanceKnob.getValue());
    const int v = static_cast<int>(volumeKnob.getValue());
    const float lvl = AlphaBalanceEffect::levelFromVolume(v);
    const float db = (lvl > 0.0f) ? juce::Decibels::gainToDecibels(lvl, -60.0f) : -60.0f;

    juce::String balStr;
    if (b == 0)       balStr = "C";
    else if (b < 0)   balStr = "L " + juce::String(-b);
    else              balStr = "R " + juce::String(b);

    juce::String dbStr;
    if (v == 0) dbStr = "-inf";
    else        dbStr = (db >= 0.0f ? "+" : "") + juce::String(db, 1) + " dB";

    const juce::String t = balStr + " / " + dbStr;
    if (t != lastBalanceText)
    {
        balanceLcd.setValue(t);
        balanceLcd.setBar(juce::jlimit(0.0f, 1.0f, lvl / 10.5f));
        lastBalanceText = t;
    }
}

void AlphaDelayEditor::showEditorFor(int which)
{
    hideEditors();

    const int b = static_cast<int>(balanceKnob.getValue());
    const int v = static_cast<int>(volumeKnob.getValue());

    const juce::Rectangle<int> lcdBounds = balanceLcd.getBounds();
    const juce::Rectangle<int> editBounds = lcdBounds
        .withTrimmedTop(16)
        .withTrimmedBottom(10);

    if (which == 1)
    {
        activeEdit = 1;
        balanceEdit.setText(juce::String(b), juce::dontSendNotification);
        balanceEdit.setBounds(editBounds);
        balanceEdit.setVisible(true);
        balanceEdit.grabKeyboardFocus();
        balanceEdit.selectAll();
    }
    else if (which == 2)
    {
        activeEdit = 2;
        volumeEdit.setText(juce::String(v), juce::dontSendNotification);
        volumeEdit.setBounds(editBounds);
        volumeEdit.setVisible(true);
        volumeEdit.grabKeyboardFocus();
        volumeEdit.selectAll();
    }
}

void AlphaDelayEditor::commitEdit(int which)
{
    if (activeEdit != which) return;

    juce::TextEditor& e = (which == 1) ? balanceEdit : volumeEdit;
    const int value = e.getText().getIntValue();

    if (which == 1 && proc.balanceParam != nullptr)
        proc.balanceParam->setValueNotifyingHost(
            proc.balanceParam->convertTo0to1(
                static_cast<float>(juce::jlimit(-128, 128, value))));
    else if (which == 2 && proc.volumeParam != nullptr)
        proc.volumeParam->setValueNotifyingHost(
            proc.volumeParam->convertTo0to1(
                static_cast<float>(juce::jlimit(0, 320, value))));

    hideEditors();
}

void AlphaDelayEditor::hideEditors()
{
    balanceEdit.setVisible(false);
    volumeEdit.setVisible(false);
    activeEdit = 0;
    grabKeyboardFocus();
}

void AlphaDelayEditor::paint(juce::Graphics& g)
{
    juce::ColourGradient grad(
        juce::Colour(20, 20, 25), 0.0f, 0.0f,
        juce::Colour(10, 10, 15), 0.0f, (float)getHeight(), false);
    g.setGradientFill(grad);
    g.fillAll();

    g.setColour(juce::Colour(40, 40, 45));
    for (int x = 0; x < getWidth(); x += 20)
        g.drawLine((float)x, 0.0f, (float)x, (float)getHeight(), 0.5f);
    for (int y = 0; y < getHeight(); y += 20)
        g.drawLine(0.0f, (float)y, (float)getWidth(), (float)y, 0.5f);

    g.setColour(juce::Colour(50, 50, 55));
    g.drawRect(getLocalBounds(), 2);

    const auto footer = getLocalBounds().removeFromBottom(24);
    g.setColour(juce::Colour(100, 100, 105));
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawFittedText("Alpha Balance",
        footer, juce::Justification::centred, 1, 0.9f);
}

void AlphaDelayEditor::resized()
{
    auto area = getLocalBounds().reduced(12);

    area.removeFromBottom(24);

    titleLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(4);

    auto presetRow = area.removeFromTop(34);
    const int btnSize = 34;
    const int gap = 6;

    prevButton.setBounds(presetRow.removeFromLeft(btnSize));
    presetRow.removeFromLeft(gap);

    const int presetLcdWidth = presetRow.getWidth() - (2 * btnSize + 2 * gap);
    presetLcd.setBounds(presetRow.removeFromLeft(presetLcdWidth));

    presetRow.removeFromLeft(gap);
    nextButton.setBounds(presetRow.removeFromLeft(btnSize));
    presetRow.removeFromLeft(gap);
    saveButton.setBounds(presetRow.removeFromLeft(btnSize));

    area.removeFromTop(14);

    juce::Slider* knobs[2]{ &balanceKnob, &volumeKnob };
    juce::Label* labels[2]{ &balanceLabel, &volumeLabel };

    const int knobSize = 116;
    const int labelH = 18;
    const int colGap = 48;
    const int totalW = 2 * knobSize + colGap;

    const int presetLcdCentreX = presetLcd.getX() + presetLcd.getWidth() / 2;
    const int originX = presetLcdCentreX - totalW / 2;
    const int originY = area.getY();

    for (int i = 0; i < 2; ++i)
    {
        const int x = originX + i * (knobSize + colGap);
        knobs[i]->setBounds(x, originY, knobSize, knobSize);
        labels[i]->setBounds(x, originY + knobSize + 2, knobSize, labelH);
    }

    const int gridH = knobSize + labelH;
    const int lcdW = 260;
    const int lcdH = 46;
    auto lcdBand = area.withTrimmedTop(gridH + 14).removeFromTop(lcdH);

    const int knobPairMidX = originX + (totalW - lcdW) / 2;
    balanceLcd.setBounds(knobPairMidX, lcdBand.getY(), lcdW, lcdH);

    if (activeEdit == 1)
    {
        balanceEdit.setBounds(balanceLcd.getBounds().withTrimmedTop(16).withTrimmedBottom(10));
    }
    else if (activeEdit == 2)
    {
        volumeEdit.setBounds(balanceLcd.getBounds().withTrimmedTop(16).withTrimmedBottom(10));
    }
}

juce::AudioProcessorEditor* AlphaDelayProcessor::createEditor()
{
    return new AlphaDelayEditor(*this);
}