#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** Card container with a caption, used for every section of the editor. */
class Card : public juce::Component
{
public:
    explicit Card (const juce::String& titleText = {}) : title (titleText) {}
    void setTitle (const juce::String& t) { title = t; repaint(); }
    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void paint (juce::Graphics&) override;

    /** Content area below the title row. */
    juce::Rectangle<int> getContentBounds() const;
    static constexpr int titleHeight = 34;

protected:
    juce::String title;
    juce::Colour accent;
};

//==============================================================================
/** Main per-band controls: style, channel mode, solo/mute/bypass and the knob row. */
class BandPanel : public Card
{
public:
    BandPanel (StSatProcessor&, int band);

    void paint (juce::Graphics&) override;
    void resized() override;
    int getBand() const noexcept { return band; }

private:
    StSatProcessor& proc;
    const int band;
    juce::Colour colour;

    std::unique_ptr<st::Dropdown> style;
    std::unique_ptr<st::Segmented> channels;
    std::unique_ptr<st::ToggleChip> solo, mute, bypass;
    std::unique_ptr<st::Knob> drive, dynamics, tone, feedback, fbFreq, mix, level, pan;

    struct Group { juce::String name; juce::Rectangle<int> area; };
    std::vector<Group> groups;
};

//==============================================================================
/** Upward / downward dynamics editor with transfer curve. */
class DynamicsPanel : public Card, private juce::Timer
{
public:
    DynamicsPanel (StSatProcessor&, int band);
    ~DynamicsPanel() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSatProcessor& proc;
    const int band;
    juce::Colour colour;

    std::unique_ptr<st::Segmented> position;
    std::unique_ptr<st::Knob> upT, upR, downT, downR, attack, release;
    std::unique_ptr<st::GainChangeMeter> meter;
    juce::Rectangle<float> graph;
    float liveIn = -100.0f, liveGain = 0.0f;

    void timerCallback() override;
    st::dsp::UpDownCompressor::Params currentParams() const;
};

//==============================================================================
/** Input / mix / output with meters. */
class MasterPanel : public Card
{
public:
    explicit MasterPanel (StSatProcessor&);
    void resized() override;

private:
        st::Knob input, output, mix, pan;
    st::LevelMeter inMeter, outMeter;
};

} // namespace stsat
