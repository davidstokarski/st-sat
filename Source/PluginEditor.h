#pragma once
#include "PluginProcessor.h"
#include "ui/MultibandDisplay.h"
#include "ui/Panels.h"

class StSatEditor : public st::EditorBase, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater
{
public:
    explicit StSatEditor (StSatProcessor&);
    ~StSatEditor() override;

    static constexpr int designWidth = 1080, designHeight = 880;

private:
    StSatProcessor& proc;

    stsat::MultibandDisplay display;
    std::unique_ptr<stsat::BandPanel> bandPanel;
    std::unique_ptr<stsat::DynamicsPanel> dynamicsPanel;
    stsat::MasterPanel masterPanel;
    st::ModulationPanel modPanel;
    std::unique_ptr<st::ModulationBinder> modBinder;

    // Footer
    juce::Label qualityLabel, slopeLabel, analyzerLabel;
    st::Segmented quality, slope;
    st::ToggleChip autoGain;
    st::PillButton analyzerOff { "OFF" }, analyzerPost { "POST" }, analyzerBoth { "PRE+POST" };
    juce::Rectangle<int> footerArea;

    int selectedBand = 0;

    void layout (juce::Rectangle<int> body) override;
    void paintOverlay (juce::Graphics&, juce::Rectangle<int>) override;

    void selectBand (int b);
    void setAnalyzerMode (int mode);

    void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
};
