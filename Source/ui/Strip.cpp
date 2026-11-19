#include "Strip.h"

namespace stsat
{

SatStrip::SatStrip (StSatProcessor& p) : proc (p)
{
    st::SpectrumAnalyzer::Options o;
    o.fftOrder = 11;
    o.hopSize = 512;
    o.numDisplayPoints = 160;
    o.minHz = 20.0f;
    o.maxHz = 20000.0f;
    o.nonConsuming = true;   // never steals the editor's analyser feed
    spectrum = std::make_unique<st::SpectrumAnalyzer> (proc.postAnalyzer, o);
    rebuild();
    startTimerHz (30);
}

SatStrip::~SatStrip() { stopTimer(); }

int SatStrip::widthFor (int height, int numBands)
{
    const int knob = juce::jlimit (54, 110, juce::roundToInt ((float) height * 0.62f));
    return 24 + (numBands + 2) * knob + 18;
}

void SatStrip::rebuild()
{
    knobs.clear();
    bands = juce::jlimit (1, kMaxBands, proc.getNumBands());
    const auto& t = st::Theme::get();
    auto make = [&] (juce::RangedAudioParameter& prm, const juce::String& label, juce::Colour c)
    {
        auto k = std::make_unique<st::Knob> (prm, &proc.undoManager, label);
        k->setSize (st::Knob::Size::small);
        k->setLook (st::Knob::Look::arc);
        k->setColour (c);
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };
    for (int b = 0; b < bands; ++b)
    {
        auto& style = proc.bandParam (b, "style");
        make (proc.bandParam (b, "drive"), style.getCurrentValueAsText().toUpperCase(), t.seriesColour (b));
    }
    make (proc.param (ids::mix), "MIX", t.accent);
    make (proc.param (ids::outputGain), "OUT", t.accent);
    resized();
}

void SatStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 6);
    if (knobs.empty()) return;
    const int w = juce::jmin (r.getWidth() / (int) knobs.size(), juce::jlimit (54, 110, juce::roundToInt ((float) getHeight() * 0.62f)));
    const int h = juce::jmin (r.getHeight(), w + 22);
    auto row = r.withSizeKeepingCentre (w * (int) knobs.size(), h);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        if (i == (size_t) bands) row.removeFromLeft (0);
        knobs[i]->setBounds (row.removeFromLeft (w));
    }
}

void SatStrip::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto area = getLocalBounds().toFloat();
    g.setColour (t.bg1);
    g.fillRect (area);

    // output spectrum behind the knobs (like a compressor module's graph)
    const auto& lv = spectrum->getLevelsDb();
    if (spectrum->getNumPoints() > 1)
    {
        juce::Path p;
        p.startNewSubPath (area.getX(), area.getBottom());
        for (int i = 0; i < spectrum->getNumPoints(); ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (spectrum->getNumPoints() - 1);
            const float y = area.getY() + area.getHeight() * juce::jlimit (0.0f, 1.0f, -lv[(size_t) i] / 90.0f);
            p.lineTo (x, y);
        }
        p.lineTo (area.getRight(), area.getBottom());
        p.closeSubPath();
        g.setColour (t.accent.withAlpha (0.10f));
        g.fillPath (p);
    }

    // a faint divider between the bands and the output knobs
    if (knobs.size() > 2)
    {
        const float x = (float) knobs[(size_t) bands]->getX();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (x - 0.5f, area.getY() + 14.0f, 1.0f, area.getHeight() - 28.0f);
    }
}

void SatStrip::timerCallback()
{
    if (juce::jlimit (1, kMaxBands, proc.getNumBands()) != bands) rebuild();
    // style labels follow the bands' styles
    for (int b = 0; b < bands && b < (int) knobs.size(); ++b)
        knobs[(size_t) b]->setLabel (proc.bandParam (b, "style").getCurrentValueAsText().toUpperCase());
    if (spectrum->update (1.0 / 30.0)) repaint();
}

} // namespace stsat
