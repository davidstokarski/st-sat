#include "Panels.h"

namespace stsat
{

//==============================================================================
FloatingBandPanel::FloatingBandPanel (StSatProcessor& p, int b)
    : proc (p), band (b), colour (st::Theme::get().seriesColour (b))
{
    auto* um = &proc.undoManager;
    auto P = [this] (const char* s) -> juce::RangedAudioParameter& { return proc.bandParam (band, s); };

    style = std::make_unique<st::Dropdown> (P ("style"), um);
    style->setLarge (true);
    style->setShowArrows (true);
    style->setAccent (colour);
    style->buildMenu = [] (int current)
    {
        juce::PopupMenu menu;
        for (auto& cat : getStyleCategories())
        {
            menu.addSectionHeader (cat);
            for (int i = 0; i < getNumStyles(); ++i)
                if (juce::String (getStyleInfo (i).category) == cat)
                    menu.addItem (juce::PopupMenu::Item (getStyleInfo (i).name).setID (i + 1).setTicked (i == current));
        }
        return menu;
    };
    style->setTooltip ("Distortion style. Click for the full list; arrows or mouse wheel step through.");
    addAndMakeVisible (*style);

    channels = std::make_unique<st::Segmented> (P ("chan"), um, juce::StringArray { "ST", "MID", "SIDE", "L", "R" });
    channels->setAccent (colour);
    channels->setTooltip ("Which part of the stereo image this band processes; the rest passes clean.");
    addAndMakeVisible (*channels);

    const auto& t = st::Theme::get();
    solo = std::make_unique<st::ToggleChip> (P ("solo"), um, "S", t.warning);
    mute = std::make_unique<st::ToggleChip> (P ("mute"), um, "M", t.danger);
    bypass = std::make_unique<st::ToggleChip> (P ("bypass"), um, "", colour);
    bypass->setIcon (st::icons::Icon::power);
    bypass->setInverted (true);
    solo->setTooltip ("Solo band");
    mute->setTooltip ("Mute band");
    bypass->setTooltip ("Band on/off");
    for (auto* c : { solo.get(), mute.get(), bypass.get() }) addAndMakeVisible (*c);

    auto knob = [&] (std::unique_ptr<st::Knob>& k, const char* id, const char* label, st::Knob::Size size, bool bipolar, const char* tip)
    {
        k = std::make_unique<st::Knob> (P (id), um, label);
        k->setColour (colour);
        k->setSize (size);
        k->setBipolar (bipolar);
        k->setTooltip (tip);
        addAndMakeVisible (*k);
    };

    knob (drive, "drive", "Drive", st::Knob::Size::large, false, "Amount of saturation for this band.");
    knob (dynamics, "dyn", "Dynamics", st::Knob::Size::large, true,
          "Upward + downward dynamics. Positive compresses (lifts quiet parts, tames loud ones), negative expands.");
    knob (tone, "tone", "Tone", st::Knob::Size::medium, true, "Tilts the saturated signal darker or brighter.");
    knob (feedback, "fb", "Feedback", st::Knob::Size::medium, false, "Resonant feedback around the saturator.");
    knob (fbFreq, "fb_freq", "Freq", st::Knob::Size::medium, false, "Feedback resonance frequency.");
    knob (mix, "mix", "Mix", st::Knob::Size::medium, false, "Dry/wet for this band.");
    knob (level, "level", "Level", st::Knob::Size::medium, true, "Output level of this band.");
    knob (pan, "pan", "Pan", st::Knob::Size::medium, true, "Stereo balance of this band.");

    knob (upT, "dyn_up_thresh", "Up Thr", st::Knob::Size::small, false, "Below this level the signal is lifted (upward compression).");
    knob (upR, "dyn_up_ratio", "Up Ratio", st::Knob::Size::small, false, "Upward compression ratio.");
    knob (attack, "dyn_attack", "Attack", st::Knob::Size::small, false, "Detector attack.");
    knob (downT, "dyn_down_thresh", "Dn Thr", st::Knob::Size::small, false, "Above this level the signal is reduced (downward compression).");
    knob (downR, "dyn_down_ratio", "Dn Ratio", st::Knob::Size::small, false, "Downward compression ratio.");
    knob (release, "dyn_release", "Release", st::Knob::Size::small, false, "Detector release.");

    position = std::make_unique<st::Segmented> (P ("dyn_pos"), um, juce::StringArray { "PRE", "POST" });
    position->setAccent (colour);
    position->setTooltip ("Apply dynamics before or after saturation.");
    addAndMakeVisible (*position);

    expandButton.setDrawBackground (true);
    expandButton.setOnColour (colour);
    expandButton.onClick = [this] { setExpanded (! expanded); };
    addAndMakeVisible (expandButton);

    startTimerHz (30);
}

void FloatingBandPanel::setExpanded (bool e)
{
    expanded = e;
    expandButton.setIcon (e ? st::icons::Icon::chevronLeft : st::icons::Icon::chevronRight);
    expandButton.setTooltip (e ? "Hide dynamics detail" : "Show dynamics detail");
    if (onExpandedChanged) onExpandedChanged (e);
}

st::dsp::UpDownCompressor::Params FloatingBandPanel::currentParams() const
{
    auto v = [this] (const char* s) { return proc.apvts.getRawParameterValue (ids::band (band, s))->load(); };
    st::dsp::UpDownCompressor::Params p;
    p.depth = v ("dyn") * 0.01f;
    p.upThresholdDb = v ("dyn_up_thresh");
    p.upRatio = v ("dyn_up_ratio");
    p.downThresholdDb = v ("dyn_down_thresh");
    p.downRatio = v ("dyn_down_ratio");
    p.kneeDb = 6.0f;
    return p;
}

void FloatingBandPanel::timerCallback()
{
    auto& bp = proc.getEngine().getBand (band);
    liveIn = bp.detectorDb.load();
    liveGain = bp.gainChangeDb.load();
    if (getWidth() > collapsedWidth + 20)
        repaint (graph.toNearestInt().expanded (8));
}

void FloatingBandPanel::resized()
{
    // Children are laid out for the fully expanded width; while collapsed the
    // dynamics section simply sits outside the (clipped) bounds and slides in.
    auto full = juce::Rectangle<int> (0, 0, expandedWidth, height);
    auto main = full.removeFromLeft (collapsedWidth).reduced (16, 12);
    separators.clear();

    // Header row
    auto header = main.removeFromTop (30);
    header.removeFromLeft (78);   // "BAND n" caption painted
    auto right = header.removeFromRight (98);
    bypass->setBounds (right.removeFromRight (30));
    right.removeFromRight (4);
    mute->setBounds (right.removeFromRight (28));
    right.removeFromRight (4);
    solo->setBounds (right.removeFromRight (28));
    header.removeFromRight (12);
    channels->setBounds (header.removeFromRight (176));
    header.removeFromRight (12);
    style->setBounds (header);

    main.removeFromTop (10);
    expandButton.setBounds (main.removeFromRight (28).withSizeKeepingCentre (28, 28).translated (0, -6));
    main.removeFromRight (6);

    // Knob row: big pair | tone + feedback | output
    auto row = main;
    auto bigArea = row.removeFromLeft (184);
    drive->setBounds (bigArea.removeFromLeft (92).reduced (2));
    dynamics->setBounds (bigArea.reduced (2));

    separators.push_back ((float) row.getX() + 8.0f);
    row.removeFromLeft (16);

    const int med = (row.getWidth() - 16) / 6;
    for (auto* k : { tone.get(), feedback.get(), fbFreq.get() })
        k->setBounds (row.removeFromLeft (med).withSizeKeepingCentre (med, juce::jmin (row.getHeight(), 84)).translated (0, 8));

    separators.push_back ((float) row.getX() + 8.0f);
    row.removeFromLeft (16);
    for (auto* k : { mix.get(), level.get(), pan.get() })
        k->setBounds (row.removeFromLeft (med).withSizeKeepingCentre (med, juce::jmin (row.getHeight(), 84)).translated (0, 8));

    // Dynamics section
    auto dyn = full.reduced (14, 12);
    separators.push_back ((float) dyn.getX() - 6.0f);
    auto dynHeader = dyn.removeFromTop (30);
    position->setBounds (dynHeader.removeFromRight (96));
    dyn.removeFromTop (10);

    graph = dyn.removeFromLeft (dyn.getHeight() + 6).toFloat();
    dyn.removeFromLeft (8);
    const int w = dyn.getWidth() / 3, h = dyn.getHeight() / 2;
    auto r1 = dyn.removeFromTop (h), r2 = dyn;
    upT->setBounds (r1.removeFromLeft (w));   upR->setBounds (r1.removeFromLeft (w));   attack->setBounds (r1);
    downT->setBounds (r2.removeFromLeft (w)); downR->setBounds (r2.removeFromLeft (w)); release->setBounds (r2);
}

void FloatingBandPanel::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    st::paint::glass (g, r, 16.0f, colour, 0.86f);

    // Band caption
    {
        auto cap = juce::Rectangle<float> (16.0f, 12.0f, 78.0f, 30.0f);
        g.setColour (colour);
        g.fillEllipse (cap.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f));
        cap.removeFromLeft (8.0f);
        st::paint::caption (g, "Band " + juce::String (band + 1), cap, t.text.withAlpha (0.85f), juce::Justification::centredLeft, 11.0f);
    }

    // Soft vertical dividers (fade at both ends)
    for (float x : separators)
    {
        if (x > r.getRight() - 4.0f) continue;
        juce::ColourGradient grad (juce::Colours::white.withAlpha (0.0f), x, 56.0f, juce::Colours::white.withAlpha (0.0f), x, (float) height - 16.0f, false);
        grad.addColour (0.5, juce::Colours::white.withAlpha (0.07f));
        g.setGradientFill (grad);
        g.fillRect (x, 56.0f, 1.0f, (float) height - 72.0f);
    }

    if (getWidth() > collapsedWidth + 20)
    {
        auto cap = juce::Rectangle<float> ((float) collapsedWidth + 14.0f, 12.0f, 120.0f, 30.0f);
        st::paint::caption (g, "Dynamics", cap, t.text.withAlpha (0.85f), juce::Justification::centredLeft, 11.0f);
        paintDynamicsGraph (g);
    }
}

void FloatingBandPanel::paintDynamicsGraph (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (graph, 10.0f);

    constexpr float lo = -72.0f, hi = 0.0f;
    auto toX = [&] (float db) { return juce::jmap (db, lo, hi, graph.getX() + 6.0f, graph.getRight() - 6.0f); };
    auto toY = [&] (float db) { return juce::jmap (juce::jlimit (lo - 12.0f, hi + 12.0f, db), lo, hi, graph.getBottom() - 6.0f, graph.getY() + 6.0f); };

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (graph, 10.0f);
    g.reduceClipRegion (clip);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (toX (lo), toY (lo), toX (hi), toY (hi), 1.0f);

    const auto p = currentParams();
    const bool active = std::abs (p.depth) > 0.0005f;

    for (float thr : { p.upThresholdDb, p.downThresholdDb })
    {
        const float x = toX (thr);
        juce::ColourGradient grad (colour.withAlpha (0.0f), x, graph.getY(), colour.withAlpha (0.0f), x, graph.getBottom(), false);
        grad.addColour (0.5, colour.withAlpha (0.25f));
        g.setGradientFill (grad);
        g.fillRect (x - 0.5f, graph.getY(), 1.0f, graph.getHeight());
    }

    juce::Path curve, area;
    area.startNewSubPath (toX (lo), graph.getBottom());
    for (int i = 0; i <= 96; ++i)
    {
        const float in = lo + (hi - lo) * (float) i / 96.0f;
        const float out = in + st::dsp::UpDownCompressor::computeStaticGainDb (in, p);
        if (i == 0) curve.startNewSubPath (toX (in), toY (out)); else curve.lineTo (toX (in), toY (out));
        area.lineTo (toX (in), toY (out));
    }
    area.lineTo (toX (hi), graph.getBottom());
    area.closeSubPath();

    g.setGradientFill (juce::ColourGradient (colour.withAlpha (active ? 0.18f : 0.05f), 0.0f, graph.getY(),
                                             colour.withAlpha (0.0f), 0.0f, graph.getBottom(), false));
    g.fillPath (area);
    st::paint::glowStroke (g, curve, colour.withAlpha (active ? 1.0f : 0.35f), 1.8f, 5.0f);

    if (active && liveIn > lo)
    {
        auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ toX (liveIn), toY (liveIn + liveGain) });
        g.setColour (colour.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (4.0f));
        g.setColour (t.text);
        g.fillEllipse (dot);
    }
    g.restoreState();

    if (! active)
    {
        g.setColour (t.textFaint);
        g.setFont (st::fonts::medium (10.5f));
        g.drawFittedText ("Turn up Dynamics\nto engage", graph.reduced (8.0f).toNearestInt(), juce::Justification::centred, 2);
    }
    else
    {
        g.setColour (liveGain >= 0.0f ? t.positive : colour);
        g.setFont (st::fonts::semibold (10.5f));
        g.drawText (st::params::formatDb (liveGain, 1), graph.reduced (8.0f, 6.0f), juce::Justification::topLeft, false);
    }
}

//==============================================================================
FooterBar::FooterBar (StSatProcessor& p)
    : quality (p.param (ids::quality), &p.undoManager, { "1x", "2x", "4x", "8x" }),
      slope (p.param (ids::slope), &p.undoManager, { "12", "24", "48" }),
      autoGain (p.param (ids::autoGain), &p.undoManager, "AUTO GAIN"),
      input (p.param (ids::inputGain), &p.undoManager, "IN"),
      mix (p.param (ids::mix), &p.undoManager, "MIX"),
      output (p.param (ids::outputGain), &p.undoManager, "OUT"),
      pan (p.param (ids::outputPan), &p.undoManager, "PAN")
{
    quality.setTooltip ("Oversampling: higher = less aliasing, more CPU.");
    slope.setTooltip ("Crossover steepness (dB/oct).");
    autoGain.setTooltip ("Keeps loudness roughly constant as you change Drive.");
    input.setTooltip ("Input gain");
    mix.setTooltip ("Global dry/wet (latency compensated)");
    output.setTooltip ("Output gain");
    pan.setTooltip ("Output balance");

    for (juce::Component* c : { (juce::Component*) &quality, (juce::Component*) &slope, (juce::Component*) &autoGain,
                                (juce::Component*) &analyzerOff, (juce::Component*) &analyzerPost, (juce::Component*) &analyzerBoth,
                                (juce::Component*) &input, (juce::Component*) &mix, (juce::Component*) &output, (juce::Component*) &pan })
        addAndMakeVisible (c);

    analyzerOff.onClick  = [this] { setAnalyzerMode (0); };
    analyzerPost.onClick = [this] { setAnalyzerMode (1); };
    analyzerBoth.onClick = [this] { setAnalyzerMode (2); };
}

void FooterBar::setAnalyzerMode (int m)
{
    analyzerOff.setToggleState (m == 0, juce::dontSendNotification);
    analyzerPost.setToggleState (m == 1, juce::dontSendNotification);
    analyzerBoth.setToggleState (m == 2, juce::dontSendNotification);
    if (onAnalyzerModeChanged) onAnalyzerModeChanged (m);
}

void FooterBar::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    // Hairline that fades out towards both edges
    juce::ColourGradient grad (juce::Colours::white.withAlpha (0.0f), 0.0f, 0.0f, juce::Colours::white.withAlpha (0.0f), (float) getWidth(), 0.0f, false);
    grad.addColour (0.5, juce::Colours::white.withAlpha (0.06f));
    g.setGradientFill (grad);
    g.fillRect (0.0f, 0.0f, (float) getWidth(), 1.0f);

    st::paint::caption (g, "Oversampling", qualityCaption.toFloat(), t.textFaint, juce::Justification::centredRight, 9.5f);
    st::paint::caption (g, "Slope", slopeCaption.toFloat(), t.textFaint, juce::Justification::centredRight, 9.5f);
}

void FooterBar::resized()
{
    auto r = getLocalBounds().reduced (14, 0).withTrimmedTop (8).withTrimmedBottom (8);

    qualityCaption = r.removeFromLeft (88);
    r.removeFromLeft (8);
    quality.setBounds (r.removeFromLeft (128));
    r.removeFromLeft (16);
    slopeCaption = r.removeFromLeft (38);
    r.removeFromLeft (8);
    slope.setBounds (r.removeFromLeft (98));
    r.removeFromLeft (14);
    autoGain.setBounds (r.removeFromLeft (92));
    r.removeFromLeft (18);
    analyzerCaption = {};
    analyzerOff.setBounds (r.removeFromLeft (40));
    r.removeFromLeft (3);
    analyzerPost.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (3);
    analyzerBoth.setBounds (r.removeFromLeft (72));

    pan.setBounds (r.removeFromRight (80));
    r.removeFromRight (6);
    output.setBounds (r.removeFromRight (86));
    r.removeFromRight (6);
    mix.setBounds (r.removeFromRight (80));
    r.removeFromRight (6);
    input.setBounds (r.removeFromRight (80));
}

} // namespace stsat
