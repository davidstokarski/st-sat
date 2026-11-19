#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** ST Sat's module in the multiview (st::MultiView, via StSatProcessor::createStripView): a drive knob per band
    (band colour, labelled with the band's style), Mix and Out, over the output spectrum. Also shown by other
    ST plug-ins' multiviews, rendered off screen, so it uses only knobs (no text entry or menus). */
class SatStrip : public juce::Component, private juce::Timer
{
public:
    explicit SatStrip (StSatProcessor&);
    ~SatStrip() override;

    /** Width for a module of this height with `numBands` bands. */
    static int widthFor (int height, int numBands);
    int getNumKnobs() const noexcept { return (int) knobs.size(); }
    st::Knob* getKnob (int i) noexcept { return juce::isPositiveAndBelow (i, (int) knobs.size()) ? knobs[(size_t) i].get() : nullptr; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    StSatProcessor& proc;
    std::vector<std::unique_ptr<st::Knob>> knobs;   // band drives, then mix, out
    std::unique_ptr<st::SpectrumAnalyzer> spectrum;
    int bands = 0;

    void rebuild();
    void timerCallback() override;
};

} // namespace stsat
