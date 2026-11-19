#include "PluginEditor.h"

using namespace stsat;

StSatEditor::StSatEditor (StSatProcessor& p)
    : EditorBase (p, designWidth, designHeight),
      proc (p),
      display (p),
      footer (p)
{
    header.setProductName ("Sat");
    header.setMultiViewCallback ([this] { toggleMultiView(); });

    auto& content = getContent();
    content.addAndMakeVisible (display);
    content.addAndMakeVisible (footer);
    footer.setBufferedToImage (true);
    header.setBufferedToImage (true);
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

    const auto reopen = popoverKind;
    closePopover();

    bandPanel = std::make_unique<FloatingBandPanel> (proc, b);
    bandPanel->setBufferedToImage (true);
    bandPanel->onDynamicsDetail = [this] { togglePopover (PopoverKind::dynamics); };
    bandPanel->onSpaceDetail = [this] { togglePopover (PopoverKind::space); };
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

    if (reopen != PopoverKind::none) togglePopover (reopen);
    if (modBinder != nullptr) modBinder->refresh();
    if (modTrails != nullptr) modTrails->refresh();
}

void StSatEditor::closePopover()
{
    popover.reset();
    popoverKind = PopoverKind::none;
}

void StSatEditor::togglePopover (PopoverKind kind)
{
    const bool same = popoverKind == kind;
    closePopover();
    if (same || kind == PopoverKind::none) return;

    auto onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StSatEditor> (this)] { if (safe != nullptr) safe->closePopover(); }); };

    if (kind == PopoverKind::dynamics)
    {
        auto p = std::make_unique<DynamicsPopover> (proc, selectedBand);
        p->onClose = onClose;
        popover = std::move (p);
    }
    else
    {
        auto p = std::make_unique<SpacePopover> (proc, selectedBand);
        p->onClose = onClose;
        popover = std::move (p);
    }

    popoverKind = kind;
    getContent().addAndMakeVisible (*popover);
    if (modTrails != nullptr) modTrails->toFront (false);
    popover->toFront (true);
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

    if (popover != nullptr)
    {
        popoverFade.setTarget (1.0f);
        popoverFade.tick (dt, 16.0f);
        const auto anchor = (popoverKind == PopoverKind::space ? bandPanel->getSpaceAnchor() : bandPanel->getDynamicsAnchor())
                            + bandPanel->getPosition();
        const int m = DynamicsPopover::margin;   // both popovers share the same shadow margin
        const int w = popover->getWidth() > 0 ? popover->getWidth() : (popoverKind == PopoverKind::space ? SpacePopover::width : DynamicsPopover::width);
        const int h = popoverKind == PopoverKind::space ? SpacePopover::height : DynamicsPopover::height;
        const int x = juce::jlimit (8 - m, designWidth - 8 + m - w, anchor.x - w / 2);
        const int y = panelY - h + m - 2 + juce::roundToInt ((1.0f - popoverFade.current) * 10.0f);
        popover->setBounds (x, y, popoverKind == PopoverKind::space ? SpacePopover::width : DynamicsPopover::width, h);
        popover->setAlpha (popoverFade.current);
    }
}

void StSatEditor::toggleMultiView()
{
    if (multiView != nullptr) { multiView.reset(); return; }
    closePopover();
    st::MultiView::Options o;
    o.leftmostKind = "ST EQ";   // EQ first on every track, then ST Sat and the others
    multiView = std::make_unique<st::MultiView> (proc, o);
    multiView->onClose = [this]
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StSatEditor> (this)] { if (safe != nullptr && safe->multiView != nullptr) safe->toggleMultiView(); });
    };
    getContent().addAndMakeVisible (*multiView);
    multiView->setBounds (getContent().getLocalBounds());
    multiView->toFront (true);
}

void StSatEditor::layout (juce::Rectangle<int>)
{
    auto all = getContent().getLocalBounds();
    display.setBounds (all.withHeight (displayHeight));
    if (multiView != nullptr) multiView->setBounds (all);
    footer.setBounds (all.removeFromBottom (footerHeight));
    if (modStrip != nullptr)
        modStrip->setBounds (0, modStripY, designWidth, st::ModulationStrip::preferredHeight);
    panelX.snap (targetPanelX());
    animate (0.0);
}
