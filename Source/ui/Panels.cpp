#include "Panels.h"

namespace stsat
{

//==============================================================================
PanStrip::PanStrip (juce::RangedAudioParameter& p, juce::UndoManager* um) : st::ParameterControl (p, um)
{
    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    setTooltip ("Band balance (double-click to centre)");
}

void PanStrip::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat();
    const bool over = isMouseOver() || dragging;

    g.setFont (st::fonts::semibold (10.5f));
    g.setColour (t.text.withAlpha (normValue < 0.49f ? 0.95f : 0.55f));
    g.drawText ("L", r.removeFromLeft (10.0f), juce::Justification::centred, false);
    g.setColour (t.text.withAlpha (normValue > 0.51f ? 0.95f : 0.55f));
    g.drawText ("R", r.removeFromRight (10.0f), juce::Justification::centred, false);

    auto track = r.reduced (5.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 10.0f, 2.0f);
    if (over)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (track, 1.0f);
    }
    const float x = track.getX() + track.getWidth() * normValue;
    g.setColour (t.text.withAlpha (over ? 0.9f : 0.0f));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ x, track.getCentreY() }));

    if (std::abs (normValue - 0.5f) > 0.01f && ! over)
    {
        g.setColour (st::Theme::get().accent2.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x, track.getCentreX()), track.getY(), std::abs (x - track.getCentreX()), 2.0f));
    }
}

void PanStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu ({}); return; }
    if (e.mods.isAltDown()) { resetToDefault(); return; }
    dragging = true;
    lastX = e.position.x;
    beginEdit();
}

void PanStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    normValue = juce::jlimit (0.0f, 1.0f, normValue + (e.position.x - lastX) / (e.mods.isShiftDown() ? 600.0f : 120.0f));
    lastX = e.position.x;
    setNormalisedDuringEdit (normValue);
    repaint();
}

void PanStrip::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    endEdit();
    repaint();
}

//==============================================================================
FloatingBandPanel::FloatingBandPanel (StSatProcessor& p, int b) : proc (p), band (b)
{
    const auto& t = st::Theme::get();
    auto* um = &proc.undoManager;
    auto P = [this] (const char* s) -> juce::RangedAudioParameter& { return proc.bandParam (band, s); };

    bypass = std::make_unique<st::ToggleChip> (P ("bypass"), um, "", t.accent2);
    bypass->setIcon (st::icons::Icon::power);
    bypass->setInverted (true);
    bypass->setGreyStyle (true);
    bypass->setTooltip ("Band on/off");

    solo = std::make_unique<st::ToggleChip> (P ("solo"), um, "S", t.warning);
    mute = std::make_unique<st::ToggleChip> (P ("mute"), um, "M", t.danger);
    solo->setGreyStyle (true);
    mute->setGreyStyle (true);
    solo->setTooltip ("Solo band");
    mute->setTooltip ("Mute band");

    channels = std::make_unique<st::Dropdown> (P ("chan"), um);
    channels->setGreyStyle (true);
    channels->setTooltip ("Process stereo, mid, side, left or right only");

    style = std::make_unique<st::Dropdown> (P ("style"), um);
    style->setGreyStyle (true);
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
    style->setTooltip ("Distortion style (mouse wheel to step)");

    for (juce::Component* c : { (juce::Component*) bypass.get(), (juce::Component*) solo.get(), (juce::Component*) mute.get(),
                                (juce::Component*) channels.get(), (juce::Component*) style.get(), (juce::Component*) &closeButton })
        addAndMakeVisible (c);

    closeButton.setMinimal (false);
    closeButton.onClick = [this] { if (onRemoveBand) onRemoveBand(); };
    closeButton.setVisible (proc.getNumBands() > 1);

    auto knob = [&] (std::unique_ptr<st::Knob>& k, const char* id, const char* label, st::Knob::Size size, st::Knob::Look look, const char* tip)
    {
        k = std::make_unique<st::Knob> (P (id), um, label);
        k->setSize (size);
        k->setLook (look);
        k->setColour (t.accent);
        k->setTooltip (tip);
        addAndMakeVisible (*k);
    };

    using L = st::Knob::Look;
    knob (mix, "mix", "Mix", st::Knob::Size::medium, L::classic, "Dry/wet for this band");
    knob (feedback, "fb", "Feedback", st::Knob::Size::medium, L::classic, "Resonant feedback around the saturator");
    knob (fbFreq, "fb_freq", "Freq", st::Knob::Size::medium, L::classic, "Feedback resonance frequency");
    knob (dynamics, "dyn", "Dynamics", st::Knob::Size::medium, L::classic,
          "Upward + downward dynamics: right compresses (lifts quiet, tames loud), left expands");
    knob (drive, "drive", "Drive", st::Knob::Size::large, L::ringed, "Amount of saturation");
    knob (level, "level", "Level", st::Knob::Size::medium, L::ringed, "Band output level");

    const char* toneIds[4] = { "tone_low", "tone_lmid", "tone_hmid", "tone_high" };
    const char* toneTips[4] = { "Low shelf (110 Hz)", "Low-mid (550 Hz)", "High-mid (2.6 kHz)", "High shelf (7.5 kHz)" };
    for (int i = 0; i < 4; ++i)
    {
        tone[(size_t) i] = std::make_unique<st::Fader> (P (toneIds[i]), um);
        tone[(size_t) i]->setTooltip (toneTips[i]);
        addAndMakeVisible (*tone[(size_t) i]);
    }

    drive->setRimParameter (&P ("drive_spread"), "Spread");
    drive->setTooltip ("Drive. Drag the red ring to spread the drive between left and right.");
    level->setRimParameter (&P ("pan"), "Pan");
    level->setTooltip ("Band level. Drag the red ring to pan the band.");

    dynDetail.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2")));
    dynDetail.setFontHeight (11.0f);
    dynDetail.onClick = [this] { if (onDynamicsDetail) onDynamicsDetail(); };
    addAndMakeVisible (dynDetail);
}

juce::Point<int> FloatingBandPanel::getDynamicsAnchor() const
{
    return { dynamics->getBounds().getCentreX(), 0 };
}

void FloatingBandPanel::resized()
{
    box = juce::Rectangle<float> (14.0f, 22.0f, (float) width - 28.0f, 62.0f);

    // Buttons straddling the top edge of the bar
    const int by = 10, bh = 23;
    bypass->setBounds (28, by, 30, bh);
    channels->setBounds (64, by, 78, bh);
    style->setBounds (148, by, 168, bh);
    closeButton.setBounds (width - 56, by, 30, bh);
    mute->setBounds (width - 90, by, 28, bh);
    solo->setBounds (width - 122, by, 28, bh);

    // Knobs hang from the bar
    mix->setBounds (22, 38, 60, 74);
    feedback->setBounds (92, 38, 66, 74);
    fbFreq->setBounds (152, 38, 60, 74);
    dynamics->setBounds (222, 38, 70, 74);
    dynDetail.setBounds (dynamics->getRight() - 18, 36, 22, 14);
    drive->setBounds (298, 2, 112, 132);

    toneArea = juce::Rectangle<int> (420, 24, 104, 96);
    for (int i = 0; i < 4; ++i)
        tone[(size_t) i]->setBounds (toneArea.getX() + i * 26, toneArea.getY(), 26, 82);

    level->setBounds (532, 18, 94, 106);
}

void FloatingBandPanel::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    // The bar: translucent dark grey with a faint edge light
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (box.translated (0.0f, 3.0f).expanded (2.0f), 9.0f);
    g.setColour (t.glassPanel);
    g.fillRoundedRectangle (box, 8.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.10f), 0.0f, box.getY(),
                                             juce::Colours::white.withAlpha (0.02f), 0.0f, box.getBottom(), false));
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    // "FEEDBACK — FREQ" link line between the two captions
    {
        const float y = (float) feedback->getBottom() - 10.0f;
        const float x0 = (float) feedback->getRight() - 2.0f, x1 = (float) fbFreq->getX() + 13.0f;
        g.setColour (t.text.withAlpha (0.5f));
        g.fillRect (x0, y, juce::jmax (0.0f, x1 - x0), 1.0f);
    }

    // Compress / expand glyphs either side of the Dynamics knob (like the original)
    {
        auto d = dynamics->getDialBounds().translated ((float) dynamics->getX(), (float) dynamics->getY());
        const float y = d.getBottom() - 2.0f;
        g.setColour (t.text.withAlpha (0.45f));
        // expand: arrows apart
        auto l = juce::Point<float> (d.getX() - 2.0f, y);
        g.drawLine (l.x - 5.0f, y, l.x + 5.0f, y, 1.0f);
        g.drawLine (l.x - 5.0f, y - 3.0f, l.x - 5.0f, y + 3.0f, 1.0f);
        g.drawLine (l.x + 5.0f, y - 3.0f, l.x + 5.0f, y + 3.0f, 1.0f);
        // compress: dense hatch
        auto r = juce::Point<float> (d.getRight() + 2.0f, y);
        for (int k = -2; k <= 2; ++k)
            g.drawLine (r.x + (float) k * 2.0f, y - 3.0f, r.x + (float) k * 2.0f, y + 3.0f, 1.0f);
    }

    // TONE caption under the faders
    st::paint::caption (g, "Tone", toneArea.toFloat().withTop ((float) toneArea.getBottom() - 16.0f).translated (0.0f, -2.0f),
                        t.text.withAlpha (0.92f), juce::Justification::centred, 11.5f);
}

//==============================================================================
DynamicsPopover::DynamicsPopover (StSatProcessor& p, int b) : proc (p), band (b)
{
    const auto& t = st::Theme::get();
    auto* um = &proc.undoManager;
    auto P = [this] (const char* s) -> juce::RangedAudioParameter& { return proc.bandParam (band, s); };

    position = std::make_unique<st::Segmented> (P ("dyn_pos"), um, juce::StringArray { "PRE", "POST" });
    position->setAccent (t.accent2);
    position->setTooltip ("Apply dynamics before or after the saturation");
    addAndMakeVisible (*position);

    auto knob = [&] (std::unique_ptr<st::Knob>& k, const char* id, const char* label, const char* tip)
    {
        k = std::make_unique<st::Knob> (P (id), um, label);
        k->setSize (st::Knob::Size::small);
        k->setTooltip (tip);
        addAndMakeVisible (*k);
    };
    knob (upT, "dyn_up_thresh", "Up Thr", "Below this level the band is lifted (upward compression)");
    knob (upR, "dyn_up_ratio", "Up Ratio", "Upward ratio");
    knob (attack, "dyn_attack", "Attack", "Detector attack");
    knob (downT, "dyn_down_thresh", "Down Thr", "Above this level the band is reduced (downward compression)");
    knob (downR, "dyn_down_ratio", "Down Ratio", "Downward ratio");
    knob (release, "dyn_release", "Release", "Detector release");

    startTimerHz (30);
}

void DynamicsPopover::timerCallback()
{
    auto& bp = proc.getEngine().getBand (band);
    liveIn = bp.detectorDb.load();
    liveGain = bp.gainChangeDb.load();
    repaint (graph.toNearestInt().expanded (6));
}

void DynamicsPopover::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto header = r.removeFromTop (24);
    position->setBounds (header.removeFromRight (96));
    r.removeFromTop (8);
    graph = r.removeFromLeft (r.getHeight()).toFloat();
    r.removeFromLeft (12);
    const int w = r.getWidth() / 3, h = r.getHeight() / 2;
    auto r1 = r.removeFromTop (h), r2 = r;
    upT->setBounds (r1.removeFromLeft (w));   upR->setBounds (r1.removeFromLeft (w));   attack->setBounds (r1);
    downT->setBounds (r2.removeFromLeft (w)); downR->setBounds (r2.removeFromLeft (w)); release->setBounds (r2);
}

void DynamicsPopover::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto bg = getLocalBounds().toFloat().reduced (1.0f);
    st::paint::softShadow (g, bg, 9.0f, 14.0f, 0.6f);
    g.setColour (juce::Colour (0xf01d181b));
    g.fillRoundedRectangle (bg, 9.0f);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (bg.reduced (0.5f), 9.0f, 1.0f);

    st::paint::caption (g, "Upward / Downward Dynamics", juce::Rectangle<float> (16.0f, 12.0f, 260.0f, 24.0f),
                        t.text.withAlpha (0.9f), juce::Justification::centredLeft, 11.0f);

    // Transfer curve
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (graph, 6.0f);

    auto v = [this] (const char* s) { return proc.apvts.getRawParameterValue (ids::band (band, s))->load(); };
    st::dsp::UpDownCompressor::Params p;
    p.depth = v ("dyn") * 0.01f;
    p.upThresholdDb = v ("dyn_up_thresh");
    p.upRatio = v ("dyn_up_ratio");
    p.downThresholdDb = v ("dyn_down_thresh");
    p.downRatio = v ("dyn_down_ratio");
    p.kneeDb = 6.0f;
    const bool active = std::abs (p.depth) > 0.0005f;

    constexpr float lo = -72.0f, hi = 0.0f;
    auto toX = [&] (float db) { return juce::jmap (db, lo, hi, graph.getX() + 6.0f, graph.getRight() - 6.0f); };
    auto toY = [&] (float db) { return juce::jmap (juce::jlimit (lo - 12.0f, hi + 12.0f, db), lo, hi, graph.getBottom() - 6.0f, graph.getY() + 6.0f); };

    g.saveState();
    g.reduceClipRegion (graph.toNearestInt());
    for (float db = -60.0f; db < 0.0f; db += 12.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRect (toX (db), graph.getY(), 1.0f, graph.getHeight());
        g.fillRect (graph.getX(), toY (db), graph.getWidth(), 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (toX (lo), toY (lo), toX (hi), toY (hi), 1.0f);

    juce::Path curve;
    for (int i = 0; i <= 96; ++i)
    {
        const float in = lo + (hi - lo) * (float) i / 96.0f;
        const float out = in + st::dsp::UpDownCompressor::computeStaticGainDb (in, p);
        if (i == 0) curve.startNewSubPath (toX (in), toY (out)); else curve.lineTo (toX (in), toY (out));
    }
    g.setColour (t.accent2.withAlpha (active ? 1.0f : 0.35f));
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    if (active && liveIn > lo)
    {
        auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ toX (liveIn), toY (liveIn + liveGain) });
        g.setColour (t.text);
        g.fillEllipse (dot);
    }
    g.restoreState();

    g.setFont (st::fonts::medium (10.5f));
    if (! active)
    {
        g.setColour (t.textFaint);
        g.drawFittedText ("Turn up Dynamics\nto engage", graph.reduced (8.0f).toNearestInt(), juce::Justification::centred, 2);
    }
    else
    {
        g.setColour (liveGain >= 0.0f ? t.positive : t.accent2);
        g.drawText (st::params::formatDb (liveGain, 1), graph.reduced (8.0f, 6.0f), juce::Justification::topLeft, false);
    }
}

//==============================================================================
FooterBar::FooterBar (StSatProcessor& p)
    : quality (p.param (ids::quality), &p.undoManager),
      slope (p.param (ids::slope), &p.undoManager),
      autoGain (p.param (ids::autoGain), &p.undoManager, "Auto Gain", st::Theme::get().positive),
      input (p.param (ids::inputGain), &p.undoManager, "In"),
      mix (p.param (ids::mix), &p.undoManager, "Mix"),
      output (p.param (ids::outputGain), &p.undoManager, "Out"),
      pan (p.param (ids::outputPan), &p.undoManager, "Pan")
{
    quality.setFlatStyle (true);
    quality.setCaption ("Oversampling");
    slope.setFlatStyle (true);
    slope.setCaption ("Slope");
    autoGain.setFlatStyle (true);
    for (auto* n : { &input, &mix, &output, &pan }) n->setFlatStyle (true);

    quality.setTooltip ("Oversampling: higher = less aliasing, more CPU");
    slope.setTooltip ("Crossover steepness");
    autoGain.setTooltip ("Keeps loudness roughly constant as you change Drive");
    input.setTooltip ("Input gain (drag)");
    mix.setTooltip ("Global dry/wet, latency compensated (drag)");
    output.setTooltip ("Output gain (drag)");
    pan.setTooltip ("Output balance (drag)");

    analyzer.setDropdownArrow (true);
    analyzer.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Off", true, analyzerMode == 0);
        m.addItem (2, "Post", true, analyzerMode == 1);
        m.addItem (3, "Pre + Post", true, analyzerMode == 2);
        st::showMenu (m, &analyzer, [safe = juce::Component::SafePointer<FooterBar> (this)] (int r) { if (safe != nullptr && r > 0) safe->setAnalyzerMode (r - 1); });
    };

    for (juce::Component* c : { (juce::Component*) &quality, (juce::Component*) &slope, (juce::Component*) &autoGain, (juce::Component*) &analyzer,
                                (juce::Component*) &input, (juce::Component*) &mix, (juce::Component*) &output, (juce::Component*) &pan })
        addAndMakeVisible (c);
}

void FooterBar::setAnalyzerMode (int m)
{
    analyzerMode = juce::jlimit (0, 2, m);
    analyzer.setButtonText (analyzerMode == 0 ? "Off" : analyzerMode == 1 ? "Post" : "Pre + Post");
    analyzer.setDimmedText ("Analyzer  ");
    if (onAnalyzerModeChanged) onAnalyzerModeChanged (analyzerMode);
}

void FooterBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();

    // Dark bar with rounded upper corners, like a tray the controls sit in.
    juce::Path bar;
    bar.addRoundedRectangle (r.getX() + 6.0f, r.getY(), r.getWidth() - 12.0f, r.getHeight() + 20.0f, 18.0f, 18.0f, true, true, false, false);
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.fillPath (bar);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), 0.0f, r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 14.0f, false));
    g.strokePath (bar, juce::PathStrokeType (1.0f));

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (float x : dividers)
        g.fillRect (x, r.getY() + 9.0f, 1.0f, r.getHeight() - 18.0f);
}

void FooterBar::resized()
{
    auto r = getLocalBounds().reduced (24, 0);
    dividers.clear();
    auto divide = [this, &r] { dividers.push_back ((float) r.getX() + 10.0f); r.removeFromLeft (20); };

    quality.setBounds (r.removeFromLeft (170));
    divide();
    slope.setBounds (r.removeFromLeft (150));
    divide();
    autoGain.setBounds (r.removeFromLeft (100));
    divide();
    analyzer.setBounds (r.removeFromLeft (128));

    pan.setBounds (r.removeFromRight (72));
    r.removeFromRight (8);
    output.setBounds (r.removeFromRight (96));
    r.removeFromRight (8);
    mix.setBounds (r.removeFromRight (82));
    r.removeFromRight (8);
    input.setBounds (r.removeFromRight (88));
    dividers.push_back ((float) input.getX() - 12.0f);
}

} // namespace stsat
