#pragma once
#include "PluginProcessor.h"
#include "ui/MultibandDisplay.h"
#include "ui/Panels.h"

class StSatEditor : public st::EditorBase, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    explicit StSatEditor (StSatProcessor&);
    ~StSatEditor() override;

    static constexpr int designWidth = 1080, designHeight = 852;

private:
    StSatProcessor& proc;

    stsat::MultibandDisplay display;
    std::unique_ptr<stsat::FloatingBandPanel> bandPanel, outgoingPanel;
    st::ModulationPanel modPanel;
    std::unique_ptr<st::ModulationBinder> modBinder;
    stsat::FooterBar footer;

    // Motion
    st::Animated panelX, panelWidth, panelFade, outgoingFade;
    bool expanded = false;
    juce::Rectangle<int> displayArea;

    int selectedBand = 0;

    void layout (juce::Rectangle<int> body) override;
    void paintBackground (juce::Graphics&, juce::Rectangle<int>) override;

    void selectBand (int b, bool animate = true);
    void animate (double dt);
    float targetPanelX() const;
    void placePanel (stsat::FloatingBandPanel&, float x, float width, float fade);

    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
};
