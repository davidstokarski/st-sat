#pragma once
#include "PluginProcessor.h"
#include "ui/MultibandDisplay.h"
#include "ui/Panels.h"

class StSatEditor : public st::EditorBase, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    explicit StSatEditor (StSatProcessor&);
    ~StSatEditor() override;

    static constexpr int designWidth = 1040, designHeight = 690;
    static constexpr int displayHeight = 478, panelY = 334, modStripY = 486, footerHeight = 40;

private:
    StSatProcessor& proc;

    stsat::MultibandDisplay display;
    std::unique_ptr<stsat::FloatingBandPanel> bandPanel, outgoingPanel;
    std::unique_ptr<juce::Component> popover;
    enum class PopoverKind { none, dynamics, space };
    PopoverKind popoverKind = PopoverKind::none;
    std::unique_ptr<st::ModulationStrip> modStrip;
    std::unique_ptr<st::ModulationBinder> modBinder;
    std::unique_ptr<st::ModulationTrails> modTrails;
    stsat::FooterBar footer;
    std::unique_ptr<st::MultiView> multiView;

    st::Animated panelX, panelFade, outgoingFade, popoverFade;
    int selectedBand = 0;

    void layout (juce::Rectangle<int> body) override;

    void selectBand (int b, bool animate = true, bool force = false);
    void togglePopover (PopoverKind);
    void closePopover();
    void toggleMultiView();

public:
    /** Opens the multiview (every ST plug-in in the session; covers the whole plugin window). */
    void openMultiView() { if (multiView == nullptr) toggleMultiView(); }
    st::MultiView* getMultiView() noexcept { return multiView.get(); }

private:
    void animate (double dt);
    float targetPanelX() const;

    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
};
