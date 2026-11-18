#include "PluginEditor.h"

using namespace stsat;

StSatEditor::StSatEditor (StSatProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p),
      display (p),
      masterPanel (p),
      modPanel (p.modMatrix),
      quality (p.param (ids::quality), &p.undoManager, { "1x", "2x", "4x", "8x" }),
      slope (p.param (ids::slope), &p.undoManager, { "12", "24", "48" }),
      autoGain (p.param (ids::autoGain), &p.undoManager, "AUTO GAIN")
{
    header.setProductName ("Sat");

    auto& content = getContent();
    content.addAndMakeVisible (display);
    content.addAndMakeVisible (masterPanel);
    content.addAndMakeVisible (modPanel);

    for (auto* l : { &qualityLabel, &slopeLabel, &analyzerLabel })
    {
        l->setFont (st::fonts::caption (9.5f));
        l->setColour (juce::Label::textColourId, st::Theme::get().textFaint);
        l->setJustificationType (juce::Justification::centredRight);
        content.addAndMakeVisible (*l);
    }
    qualityLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    slopeLabel.setText ("SLOPE dB/OCT", juce::dontSendNotification);
    analyzerLabel.setText ("ANALYZER", juce::dontSendNotification);

    quality.setTooltip ("Oversampling. Higher settings reduce aliasing at the cost of CPU and a little latency.");
    slope.setTooltip ("Crossover steepness between bands.");
    autoGain.setTooltip ("Keeps loudness roughly constant as you change Drive.");
    content.addAndMakeVisible (quality);
    content.addAndMakeVisible (slope);
    content.addAndMakeVisible (autoGain);

    for (auto* b : { &analyzerOff, &analyzerPost, &analyzerBoth })
    {
        b->setClickingTogglesState (false);
        content.addAndMakeVisible (*b);
    }
    analyzerOff.onClick  = [this] { setAnalyzerMode (0); };
    analyzerPost.onClick = [this] { setAnalyzerMode (1); };
    analyzerBoth.onClick = [this] { setAnalyzerMode (2); };

    display.onBandSelected = [this] (int b) { selectBand (b); };

    selectedBand = juce::jlimit (0, proc.getNumBands() - 1, (int) proc.uiState.getProperty ("selectedBand", 0));
    setAnalyzerMode ((int) proc.uiState.getProperty ("analyzer", 1));
    selectBand (selectedBand);

    modBinder = std::make_unique<st::ModulationBinder> (proc.modMatrix, getContent(), &modPanel);
    relayout();

    proc.param (ids::numBands).addListener (this);
}

StSatEditor::~StSatEditor()
{
    modBinder.reset();
    proc.param (ids::numBands).removeListener (this);
    cancelPendingUpdate();
}

void StSatEditor::handleAsyncUpdate()
{
    const int nb = proc.getNumBands();
    if (selectedBand >= nb)
        selectBand (nb - 1);
}

void StSatEditor::selectBand (int b)
{
    b = juce::jlimit (0, juce::jmax (0, proc.getNumBands() - 1), b);
    const bool changed = bandPanel == nullptr || bandPanel->getBand() != b;
    selectedBand = b;
    proc.uiState.setProperty ("selectedBand", b, nullptr);
    display.setSelectedBand (b);

    if (changed)
    {
        bandPanel = std::make_unique<BandPanel> (proc, b);
        dynamicsPanel = std::make_unique<DynamicsPanel> (proc, b);
        getContent().addAndMakeVisible (*bandPanel);
        getContent().addAndMakeVisible (*dynamicsPanel);
        getContent().resized();
        if (modBinder != nullptr) modBinder->refresh();
    }
}

void StSatEditor::setAnalyzerMode (int mode)
{
    mode = juce::jlimit (0, 2, mode);
    proc.uiState.setProperty ("analyzer", mode, nullptr);
    display.setAnalyzerMode ((MultibandDisplay::AnalyzerMode) mode);
    analyzerOff.setToggleState (mode == 0, juce::dontSendNotification);
    analyzerPost.setToggleState (mode == 1, juce::dontSendNotification);
    analyzerBoth.setToggleState (mode == 2, juce::dontSendNotification);
}

void StSatEditor::layout (juce::Rectangle<int> body)
{
    auto r = body.reduced (14, 0).withTrimmedTop (12);

    footerArea = r.removeFromBottom (40);
    r.removeFromBottom (8);

    display.setBounds (r.removeFromTop (300));
    r.removeFromTop (12);

    auto row = r.removeFromTop (214);
    masterPanel.setBounds (row.removeFromRight (168));
    row.removeFromRight (12);
    if (dynamicsPanel != nullptr) dynamicsPanel->setBounds (row.removeFromRight (322));
    row.removeFromRight (12);
    if (bandPanel != nullptr) bandPanel->setBounds (row);

    r.removeFromTop (12);
    modPanel.setBounds (r);

    // Footer
    auto f = footerArea.reduced (2, 6);
    qualityLabel.setBounds (f.removeFromLeft (92));
    f.removeFromLeft (8);
    quality.setBounds (f.removeFromLeft (156));
    f.removeFromLeft (22);
    slopeLabel.setBounds (f.removeFromLeft (86));
    f.removeFromLeft (8);
    slope.setBounds (f.removeFromLeft (120));
    f.removeFromLeft (22);
    autoGain.setBounds (f.removeFromLeft (96));

    auto right = f.removeFromRight (250);
    analyzerBoth.setBounds (right.removeFromRight (80));
    right.removeFromRight (4);
    analyzerPost.setBounds (right.removeFromRight (56));
    right.removeFromRight (4);
    analyzerOff.setBounds (right.removeFromRight (46));
    right.removeFromRight (8);
    analyzerLabel.setBounds (right);
}

void StSatEditor::paintOverlay (juce::Graphics& g, juce::Rectangle<int>)
{
    // Hairline above the footer
    const auto& t = st::Theme::get();
    g.setColour (t.stroke.withAlpha (0.6f));
    g.fillRect ((float) footerArea.getX(), (float) footerArea.getY() - 1.0f, (float) footerArea.getWidth(), 1.0f);
}
