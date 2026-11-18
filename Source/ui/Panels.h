#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** Translucent panel that floats over the display and follows the selected band.
    The dynamics section slides out of its right-hand side. */
class FloatingBandPanel : public juce::Component, private juce::Timer
{
public:
    FloatingBandPanel (StSatProcessor&, int band);
    ~FloatingBandPanel() override { stopTimer(); }

    int getBand() const noexcept { return band; }

    static constexpr int collapsedWidth = 652, expandedWidth = 984, height = 200;

    bool isExpanded() const noexcept { return expanded; }
    void setExpanded (bool);
    std::function<void (bool)> onExpandedChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSatProcessor& proc;
    const int band;
    const juce::Colour colour;
    bool expanded = false;

    std::unique_ptr<st::Dropdown> style;
    std::unique_ptr<st::Segmented> channels, position;
    std::unique_ptr<st::ToggleChip> solo, mute, bypass;
    std::unique_ptr<st::Knob> drive, dynamics, tone, feedback, fbFreq, mix, level, pan;
    std::unique_ptr<st::Knob> upT, upR, downT, downR, attack, release;
    st::IconButton expandButton { st::icons::Icon::chevronRight, "Show dynamics detail" };

    juce::Rectangle<float> graph;
    std::vector<float> separators;   // x positions of soft dividers
    float liveIn = -100.0f, liveGain = 0.0f;

    void timerCallback() override;
    st::dsp::UpDownCompressor::Params currentParams() const;
    void paintDynamicsGraph (juce::Graphics&);
};

//==============================================================================
/** Bottom strip: quality, slope, auto gain, analyser and the master controls. */
class FooterBar : public juce::Component
{
public:
    explicit FooterBar (StSatProcessor&);
    std::function<void (int)> onAnalyzerModeChanged;
    void setAnalyzerMode (int);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    st::Segmented quality, slope;
    st::ToggleChip autoGain;
    st::PillButton analyzerOff { "OFF" }, analyzerPost { "POST" }, analyzerBoth { "PRE+POST" };
    st::NumberBox input, mix, output, pan;
    juce::Rectangle<int> qualityCaption, slopeCaption, analyzerCaption;
};

} // namespace stsat
