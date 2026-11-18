#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** Tiny "L ── R" balance control drawn under the Level knob. */
class PanStrip : public st::ParameterControl
{
public:
    PanStrip (juce::RangedAudioParameter&, juce::UndoManager*);
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { resetToDefault(); }

private:
    bool dragging = false;
    float lastX = 0.0f;
};

//==============================================================================
/** The band control bar that floats over the display (Saturn-style): a short translucent bar
    with grey buttons sitting on its top edge and knobs hanging from it. */
class FloatingBandPanel : public juce::Component
{
public:
    FloatingBandPanel (StSatProcessor&, int band);

    int getBand() const noexcept { return band; }
    static constexpr int width = 660, height = 136;

    /** Called when the user clicks the Dynamics caption (to open the detail popover). */
    std::function<void()> onDynamicsDetail;
    std::function<void()> onRemoveBand;
    juce::Point<int> getDynamicsAnchor() const;   // local, top-centre of the dynamics knob

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSatProcessor& proc;
    const int band;

    std::unique_ptr<st::ToggleChip> bypass, solo, mute;
    std::unique_ptr<st::Dropdown> channels, style;
    st::IconButton closeButton { st::icons::Icon::close, "Remove band" };
    std::unique_ptr<st::Knob> mix, feedback, fbFreq, dynamics, drive, level;
    std::array<std::unique_ptr<st::Fader>, 4> tone;
    st::FlatButton dynDetail { "", "Upward / downward dynamics detail" };

    juce::Rectangle<float> box;
    juce::Rectangle<int> toneArea;
};

//==============================================================================
/** Upward / downward dynamics editor shown as a popover above the band panel. */
class DynamicsPopover : public juce::Component, private juce::Timer
{
public:
    DynamicsPopover (StSatProcessor&, int band);
    ~DynamicsPopover() override { stopTimer(); }
    static constexpr int width = 430, height = 182;
    int getBand() const noexcept { return band; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSatProcessor& proc;
    const int band;
    std::unique_ptr<st::Segmented> position;
    std::unique_ptr<st::Knob> upT, upR, downT, downR, attack, release;
    juce::Rectangle<float> graph;
    float liveIn = -100.0f, liveGain = 0.0f;

    void timerCallback() override;
};

//==============================================================================
/** Bottom bar: text-only settings on the left, master values on the right. */
class FooterBar : public juce::Component
{
public:
    explicit FooterBar (StSatProcessor&);
    std::function<void (int)> onAnalyzerModeChanged;
    void setAnalyzerMode (int);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    st::Dropdown quality, slope;
    st::ToggleChip autoGain;
    st::FlatButton analyzer { "", "Spectrum analyser" };
    st::NumberBox input, mix, output, pan;
    int analyzerMode = 1;
    std::vector<float> dividers;
};

} // namespace stsat
