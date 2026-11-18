#include "PluginEditor.h"

using namespace stsat;

StSatEditor::StSatEditor (StSatProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p),
      display (p),
      footer (p)
{
    header.setProductName ("Sat");

    auto& content = getContent();
    content.addAndMakeVisible (display);
    content.addAndMakeVisible (footer);
    header.toFront (false);

    display.setHandleRange (104.0f, (float) panelY - 24.0f);
    display.onBandSelected = [this] (int b) { selectBand (b); };
    display.onFrame = [this] (double dt) { animate (dt); };

    footer.onAnalyzerModeChanged = [this] (int m)
    {
        proc.uiState.setProperty ("analyzer", m, nullptr);
        display.setAnalyzerMode ((MultibandDisplay::AnalyzerMode) juce::jlimit (0, 2, m));
    };
    footer.setAnalyzerMode ((int) proc.uiState.getProperty ("analyzer", 1));

    panelFade.snap (1.0f);
    selectBand (juce::jlimit (0, proc.getNumBands() - 1, (int) proc.uiState.getProperty ("selectedBand", 0)), false);

    modStrip = std::make_unique<st::ModulationStrip> (proc.modMatrix);
    getContent().addAndMakeVisible (*modStrip);
    modBinder = std::make_unique<st::ModulationBinder> (proc.modMatrix, getContent());
    modBinder->attachStrip (modStrip.get());
    modTrails = std::make_unique<st::ModulationTrails> (proc.modMatrix, *modStrip, getContent());
    header.toFront (false);
    proc.param (ids::numBands).addListener (this);
    relayout();
}

StSatEditor::~StSatEditor()
{
    proc.param (ids::numBands).removeListener (this);
    cancelPendingUpdate();
    modTrails.reset();
    modBinder.reset();
    modStrip.reset();
}

void StSatEditor::handleAsyncUpdate()
{
    const int nb = proc.getNumBands();
    // Band count changed: rebuild the panel (its remove button depends on the count).
    selectBand (juce::jmin (selectedBand, nb - 1), true, true);
}

void StSatEditor::selectBand (int b, bool animateChange, bool force)
{
    b = juce::jlimit (0, juce::jmax (0, proc.getNumBands() - 1), b);
    selectedBand = b;
    proc.uiState.setProperty ("selectedBand", b, nullptr);
    display.setSelectedBand (b);

    if (! force && bandPanel != nullptr && bandPanel->getBand() == b)
        return;

    if (animateChange && bandPanel != nullptr)
    {
        outgoingPanel = std::move (bandPanel);
        outgoingPanel->setInterceptsMouseClicks (false, false);
        outgoingFade.snap (1.0f);
    }

    const bool popoverWasOpen = dynPopover != nullptr;
    dynPopover.reset();

    bandPanel = std::make_unique<FloatingBandPanel> (proc, b);
    bandPanel->onDynamicsDetail = [this] { toggleDynamicsPopover(); };
    bandPanel->onRemoveBand = [this]
    {
        const int band = selectedBand;
        juce::MessageManager::callAsync ([this, band] { proc.removeBand (band); });
    };
    getContent().addAndMakeVisible (*bandPanel);
    header.toFront (false);
    if (modTrails != nullptr) modTrails->toFront (false);

    if (animateChange && outgoingPanel != nullptr)
        panelFade.snap (0.0f);
    else
    {
        panelX.snap (targetPanelX());
        panelFade.snap (1.0f);
    }

    bandPanel->setBounds (juce::roundToInt (panelX.current), panelY, FloatingBandPanel::width, FloatingBandPanel::height);

    if (popoverWasOpen) toggleDynamicsPopover();
    if (modBinder != nullptr) modBinder->refresh();
    if (modTrails != nullptr) modTrails->refresh();
}

void StSatEditor::toggleDynamicsPopover()
{
    if (dynPopover != nullptr)
    {
        dynPopover.reset();
        return;
    }

    dynPopover = std::make_unique<DynamicsPopover> (proc, selectedBand);
    getContent().addAndMakeVisible (*dynPopover);
    if (modTrails != nullptr) modTrails->refresh();
    popoverFade.snap (0.0f);
    animate (0.0);
    if (modBinder != nullptr) modBinder->refresh();
}

float StSatEditor::targetPanelX() const
{
    const float w = (float) FloatingBandPanel::width;
    const float centre = display.getBandCentreX (selectedBand);
    return juce::jlimit (8.0f, (float) designWidth - 8.0f - w, centre - w * 0.5f);
}

void StSatEditor::animate (double dt)
{
    if (bandPanel == nullptr) return;

    panelX.setTarget (targetPanelX());
    panelX.tick (dt, 11.0f);
    panelFade.setTarget (1.0f);
    panelFade.tick (dt, 14.0f);

    bandPanel->setBounds (juce::roundToInt (panelX.current), panelY + juce::roundToInt ((1.0f - panelFade.current) * 8.0f),
                          FloatingBandPanel::width, FloatingBandPanel::height);
    bandPanel->setAlpha (panelFade.current);

    if (outgoingPanel != nullptr)
    {
        outgoingFade.setTarget (0.0f);
        if (! outgoingFade.tick (dt, 18.0f) || outgoingFade.current < 0.02f) outgoingPanel.reset();
        else outgoingPanel->setAlpha (outgoingFade.current);
    }

    if (dynPopover != nullptr)
    {
        popoverFade.setTarget (1.0f);
        popoverFade.tick (dt, 16.0f);
        const auto anchor = bandPanel->getDynamicsAnchor() + bandPanel->getPosition();
        const int x = juce::jlimit (8, designWidth - 8 - DynamicsPopover::width, anchor.x - DynamicsPopover::width / 2);
        const int y = panelY - DynamicsPopover::height - 2 + juce::roundToInt ((1.0f - popoverFade.current) * 10.0f);
        dynPopover->setBounds (x, y, DynamicsPopover::width, DynamicsPopover::height);
        dynPopover->setAlpha (popoverFade.current);
    }
}

void StSatEditor::layout (juce::Rectangle<int>)
{
    auto all = getContent().getLocalBounds();
    display.setBounds (all.withHeight (displayHeight));
    footer.setBounds (all.removeFromBottom (footerHeight));
    if (modStrip != nullptr)
        modStrip->setBounds (0, modStripY, designWidth, st::ModulationStrip::preferredHeight);
    panelX.snap (targetPanelX());
    animate (0.0);
}
