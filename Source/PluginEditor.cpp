#include "PluginEditor.h"

using namespace stsat;

StSatEditor::StSatEditor (StSatProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p),
      display (p),
      modPanel (p.modMatrix),
      footer (p)
{
    header.setProductName ("Sat");

    auto& content = getContent();
    content.addAndMakeVisible (display);
    content.addAndMakeVisible (modPanel);
    content.addAndMakeVisible (footer);
    modPanel.setBorderless (true);

    display.onBandSelected = [this] (int b) { selectBand (b); };
    display.onFrame = [this] (double dt) { animate (dt); };

    footer.onAnalyzerModeChanged = [this] (int m)
    {
        proc.uiState.setProperty ("analyzer", m, nullptr);
        display.setAnalyzerMode ((MultibandDisplay::AnalyzerMode) juce::jlimit (0, 2, m));
    };
    footer.setAnalyzerMode ((int) proc.uiState.getProperty ("analyzer", 1));

    expanded = (bool) proc.uiState.getProperty ("dynExpanded", false);
    panelWidth.snap ((float) (expanded ? FloatingBandPanel::expandedWidth : FloatingBandPanel::collapsedWidth));
    panelFade.snap (1.0f);

    selectBand (juce::jlimit (0, proc.getNumBands() - 1, (int) proc.uiState.getProperty ("selectedBand", 0)), false);

    modBinder = std::make_unique<st::ModulationBinder> (proc.modMatrix, getContent(), &modPanel);
    proc.param (ids::numBands).addListener (this);
    relayout();
}

StSatEditor::~StSatEditor()
{
    proc.param (ids::numBands).removeListener (this);
    cancelPendingUpdate();
    modBinder.reset();
}

void StSatEditor::handleAsyncUpdate()
{
    const int nb = proc.getNumBands();
    if (selectedBand >= nb)
        selectBand (nb - 1);
}

void StSatEditor::selectBand (int b, bool animateChange)
{
    b = juce::jlimit (0, juce::jmax (0, proc.getNumBands() - 1), b);
    selectedBand = b;
    proc.uiState.setProperty ("selectedBand", b, nullptr);
    display.setSelectedBand (b);

    if (bandPanel != nullptr && bandPanel->getBand() == b)
        return;

    // Cross-fade: the old panel fades out in place while the new one glides to its band.
    if (animateChange && bandPanel != nullptr)
    {
        outgoingPanel = std::move (bandPanel);
        outgoingPanel->setInterceptsMouseClicks (false, false);
        outgoingFade.snap (1.0f);
    }

    bandPanel = std::make_unique<FloatingBandPanel> (proc, b);
    bandPanel->setExpanded (expanded);
    bandPanel->onExpandedChanged = [this] (bool e)
    {
        expanded = e;
        proc.uiState.setProperty ("dynExpanded", e, nullptr);
    };
    getContent().addAndMakeVisible (*bandPanel);
    bandPanel->setBounds (0, 0, FloatingBandPanel::expandedWidth, FloatingBandPanel::height);

    if (animateChange && outgoingPanel != nullptr)
    {
        panelFade.snap (0.0f);
        // Keep the new panel's start position at the old one so it slides across.
    }
    else
    {
        panelX.snap (targetPanelX());
        panelFade.snap (1.0f);
    }

    if (modBinder != nullptr) modBinder->refresh();
    if (! displayArea.isEmpty()) animate (0.0);
}

float StSatEditor::targetPanelX() const
{
    const float w = panelWidth.target;
    const float centre = (float) displayArea.getX() + display.getBandCentreX (selectedBand);
    const float minX = (float) displayArea.getX() + 14.0f;
    const float maxX = (float) displayArea.getRight() - 14.0f - w;
    return juce::jlimit (minX, juce::jmax (minX, maxX), centre - w * 0.5f);
}

void StSatEditor::placePanel (FloatingBandPanel& p, float x, float width, float fade)
{
    const int y = displayArea.getBottom() - FloatingBandPanel::height - 12;
    p.setBounds (juce::roundToInt (x), y + juce::roundToInt ((1.0f - fade) * 10.0f), juce::roundToInt (width), FloatingBandPanel::height);
    p.setAlpha (juce::jlimit (0.0f, 1.0f, fade));
}

void StSatEditor::animate (double dt)
{
    if (bandPanel == nullptr || displayArea.isEmpty()) return;

    panelWidth.setTarget ((float) (expanded ? FloatingBandPanel::expandedWidth : FloatingBandPanel::collapsedWidth));
    panelWidth.tick (dt, 14.0f);
    panelX.setTarget (targetPanelX());
    panelX.tick (dt, 12.0f);
    panelFade.setTarget (1.0f);
    panelFade.tick (dt, 14.0f);

    placePanel (*bandPanel, panelX.current, panelWidth.current, panelFade.current);

    if (outgoingPanel != nullptr)
    {
        outgoingFade.setTarget (0.0f);
        if (! outgoingFade.tick (dt, 18.0f) || outgoingFade.current < 0.02f)
            outgoingPanel.reset();
        else
            outgoingPanel->setAlpha (outgoingFade.current);
    }

    display.setPanelArea (bandPanel->getBounds().toFloat().translated ((float) -displayArea.getX(), (float) -displayArea.getY())
                                                .withWidth ((float) displayArea.getWidth()).withX (0.0f));
}

void StSatEditor::layout (juce::Rectangle<int> body)
{
    auto r = body;
    footer.setBounds (r.removeFromBottom (44));
    auto modArea = r.removeFromBottom (st::ModulationPanel::preferredHeight);
    modPanel.setBounds (modArea.reduced (14, 0));
    displayArea = r;
    display.setBounds (displayArea);

    if (bandPanel != nullptr)
    {
        panelX.snap (targetPanelX());
        placePanel (*bandPanel, panelX.current, panelWidth.current, panelFade.current);
    }
}

void StSatEditor::paintBackground (juce::Graphics& g, juce::Rectangle<int> area)
{
    EditorBase::paintBackground (g, area);

    // A soft, coloured ambient glow under the selected band (follows the panel).
    if (bandPanel != nullptr && ! displayArea.isEmpty())
    {
        const auto c = st::Theme::get().seriesColour (selectedBand);
        const float cx = panelX.current + panelWidth.current * 0.5f;
        const float cy = (float) displayArea.getBottom();
        juce::ColourGradient glow (c.withAlpha (0.07f), cx, cy, c.withAlpha (0.0f), cx + 420.0f, cy, true);
        g.setGradientFill (glow);
        g.fillRect (area.toFloat());
    }
}
