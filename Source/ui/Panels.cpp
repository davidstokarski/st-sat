#include "Panels.h"

namespace stsat
{

//==============================================================================
void Card::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto r = getLocalBounds().toFloat();
    st::paint::softShadow (g, r.reduced (2.0f), t.radiusLarge, 8.0f, 0.35f);
    st::paint::panel (g, r, t.radiusLarge, t.bg2);

    if (title.isNotEmpty())
    {
        auto titleRow = r.removeFromTop ((float) titleHeight).reduced (16.0f, 0.0f);
        if (! accent.isTransparent())
        {
            g.setColour (accent);
            g.fillEllipse (titleRow.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
            titleRow.removeFromLeft (8.0f);
        }
        st::paint::caption (g, title, titleRow, t.textDim, juce::Justification::centredLeft, 10.5f);
    }
}

juce::Rectangle<int> Card::getContentBounds() const
{
    return getLocalBounds().withTrimmedTop (title.isNotEmpty() ? titleHeight : 10).reduced (14, 0).withTrimmedBottom (12);
}

//==============================================================================
BandPanel::BandPanel (StSatProcessor& p, int b) : Card ("Band " + juce::String (b + 1)), proc (p), band (b)
{
    colour = st::Theme::get().seriesColour (b);
    setAccent (colour);
    auto* um = &proc.undoManager;
    auto P = [this] (const char* s) -> juce::RangedAudioParameter& { return proc.bandParam (band, s); };

    style = std::make_unique<st::Dropdown> (P ("style"), um);
    style->setLarge (true);
    style->setShowArrows (true);
    style->setAccent (colour);
    style->buildMenu = [] (int current)
    {
        juce::PopupMenu menu;
        const auto cats = getStyleCategories();
        for (auto& cat : cats)
        {
            menu.addSectionHeader (cat);
            for (int i = 0; i < getNumStyles(); ++i)
                if (juce::String (getStyleInfo (i).category) == cat)
                    menu.addItem (juce::PopupMenu::Item (getStyleInfo (i).name).setID (i + 1).setTicked (i == current));
        }
        return menu;
    };
    style->setTooltip ("Distortion style. Click for the full list, or use the arrows / mouse wheel to step through.");
    addAndMakeVisible (*style);

    channels = std::make_unique<st::Segmented> (P ("chan"), um, juce::StringArray { "ST", "MID", "SIDE", "L", "R" });
    channels->setAccent (colour);
    channels->setTooltip ("Which part of the stereo signal this band processes. The rest passes through clean.");
    addAndMakeVisible (*channels);

    solo = std::make_unique<st::ToggleChip> (P ("solo"), um, "S", st::Theme::get().warning);
    mute = std::make_unique<st::ToggleChip> (P ("mute"), um, "M", st::Theme::get().danger);
    bypass = std::make_unique<st::ToggleChip> (P ("bypass"), um, "", colour);
    bypass->setIcon (st::icons::Icon::power);
    bypass->setInverted (true);
    solo->setTooltip ("Solo band");
    mute->setTooltip ("Mute band");
    bypass->setTooltip ("Band on/off (off passes the band through unprocessed)");
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
          "Upward + downward dynamics depth. Positive compresses (lifts quiet, tames loud), negative expands. Fine-tune it in the Dynamics panel.");
    knob (tone, "tone", "Tone", st::Knob::Size::medium, true, "Tilts the saturated signal darker or brighter.");
    knob (feedback, "fb", "Amt", st::Knob::Size::medium, false, "Resonant feedback around the saturator.");
    knob (fbFreq, "fb_freq", "Freq", st::Knob::Size::medium, false, "Centre frequency of the feedback resonance.");
    knob (mix, "mix", "Mix", st::Knob::Size::medium, false, "Dry/wet for this band.");
    knob (level, "level", "Level", st::Knob::Size::medium, false, "Output level of this band.");
    knob (pan, "pan", "Pan", st::Knob::Size::medium, true, "Stereo balance of this band.");
}

void BandPanel::paint (juce::Graphics& g)
{
    Card::paint (g);
    const auto& t = st::Theme::get();

    for (auto& gr : groups)
    {
        auto a = gr.area.toFloat();
        auto cap = a.removeFromTop (14.0f);
        st::paint::caption (g, gr.name, cap, t.textFaint, juce::Justification::centred, 9.5f);
        g.setColour (t.stroke);
        const float w = st::fonts::stringWidth (st::fonts::caption (9.5f), gr.name.toUpperCase());
        g.fillRect (cap.getX() + 4.0f, cap.getCentreY(), juce::jmax (0.0f, (cap.getWidth() - w) * 0.5f - 12.0f), 1.0f);
        g.fillRect (cap.getCentreX() + w * 0.5f + 8.0f, cap.getCentreY(), juce::jmax (0.0f, (cap.getWidth() - w) * 0.5f - 12.0f), 1.0f);
    }

    // Divider between the big pair and the rest
    if (groups.size() > 1)
    {
        g.setColour (t.stroke.withAlpha (0.6f));
        const float x = (float) groups[0].area.getRight() + 9.0f;
        g.fillRect (x, (float) groups[0].area.getY() + 8.0f, 1.0f, (float) groups[0].area.getHeight() - 16.0f);
    }
}

void BandPanel::resized()
{
    auto r = getLocalBounds();
    auto titleRow = r.removeFromTop (titleHeight + 6).reduced (14, 6).withTrimmedTop (2);
    titleRow.removeFromLeft (70);

    auto right = titleRow.removeFromRight (100);
    bypass->setBounds (right.removeFromRight (30));
    right.removeFromRight (5);
    mute->setBounds (right.removeFromRight (28));
    right.removeFromRight (5);
    solo->setBounds (right.removeFromRight (28));

    titleRow.removeFromRight (12);
    style->setBounds (titleRow);

    auto content = r.reduced (14, 0).withTrimmedBottom (10).withTrimmedTop (6);
    groups.clear();

    // Big pair
    auto big = content.removeFromLeft (196);
    groups.push_back ({ "Character", big });
    {
        auto k = big.withTrimmedTop (16);
        drive->setBounds (k.removeFromLeft (98).reduced (2));
        dynamics->setBounds (k.reduced (2));
    }
    content.removeFromLeft (18);

    // Channel selector across the top of the right-hand area
    channels->setBounds (content.removeFromTop (26).withTrimmedLeft (4));
    content.removeFromTop (6);

    const int total = content.getWidth();
    auto addGroup = [&] (const char* name, std::initializer_list<st::Knob*> knobs, int width)
    {
        auto area = content.removeFromLeft (width);
        groups.push_back ({ name, area.reduced (2, 0) });
        auto k = area.withTrimmedTop (16);
        const int each = k.getWidth() / (int) knobs.size();
        for (auto* kn : knobs)
            kn->setBounds (k.removeFromLeft (each).withSizeKeepingCentre (each, juce::jmin (k.getHeight(), 84)));
    };

    addGroup ("Tone", { tone.get() }, total * 2 / 9);
    addGroup ("Feedback", { feedback.get(), fbFreq.get() }, total * 3 / 9);
    addGroup ("Output", { mix.get(), level.get(), pan.get() }, content.getWidth());
}

//==============================================================================
DynamicsPanel::DynamicsPanel (StSatProcessor& p, int b) : Card ("Dynamics"), proc (p), band (b)
{
    colour = st::Theme::get().seriesColour (b);
    auto* um = &proc.undoManager;
    auto P = [this] (const char* s) -> juce::RangedAudioParameter& { return proc.bandParam (band, s); };

    position = std::make_unique<st::Segmented> (P ("dyn_pos"), um, juce::StringArray { "PRE", "POST" });
    position->setAccent (colour);
    position->setTooltip ("Apply dynamics before or after the saturation.");
    addAndMakeVisible (*position);

    auto knob = [&] (std::unique_ptr<st::Knob>& k, const char* id, const char* label, const char* tip)
    {
        k = std::make_unique<st::Knob> (P (id), um, label);
        k->setColour (colour);
        k->setSize (st::Knob::Size::small);
        k->setTooltip (tip);
        addAndMakeVisible (*k);
    };

    knob (upT, "dyn_up_thresh", "Up Thr", "Signals below this level are lifted (upward compression).");
    knob (upR, "dyn_up_ratio", "Up Ratio", "Upward compression ratio.");
    knob (downT, "dyn_down_thresh", "Dn Thr", "Signals above this level are reduced (downward compression).");
    knob (downR, "dyn_down_ratio", "Dn Ratio", "Downward compression ratio.");
    knob (attack, "dyn_attack", "Attack", "Detector attack time.");
    knob (release, "dyn_release", "Release", "Detector release time.");

    meter = std::make_unique<st::GainChangeMeter> ([this] { return proc.getEngine().getBand (band).gainChangeDb.load(); }, 24.0f);
    meter->setColour (colour);
    addAndMakeVisible (*meter);

    startTimerHz (30);
}

st::dsp::UpDownCompressor::Params DynamicsPanel::currentParams() const
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

void DynamicsPanel::timerCallback()
{
    auto& bp = proc.getEngine().getBand (band);
    const float in = bp.detectorDb.load();
    const float gc = bp.gainChangeDb.load();
    if (std::abs (in - liveIn) > 0.1f || std::abs (gc - liveGain) > 0.1f)
    {
        liveIn = in;
        liveGain = gc;
    }
    repaint (graph.toNearestInt().expanded (6));
}

void DynamicsPanel::paint (juce::Graphics& g)
{
    Card::paint (g);
    const auto& t = st::Theme::get();

    st::paint::well (g, graph, t.radius);

    constexpr float lo = -72.0f, hi = 0.0f;
    auto toX = [&] (float db) { return juce::jmap (db, lo, hi, graph.getX() + 4.0f, graph.getRight() - 4.0f); };
    auto toY = [&] (float db) { return juce::jmap (juce::jlimit (lo - 12.0f, hi + 12.0f, db), lo, hi, graph.getBottom() - 4.0f, graph.getY() + 4.0f); };

    g.saveState();
    g.reduceClipRegion (graph.reduced (1.0f).toNearestInt());

    // grid
    for (float db = -60.0f; db < 0.0f; db += 12.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f));
        g.fillRect (toX (db), graph.getY(), 1.0f, graph.getHeight());
        g.fillRect (graph.getX(), toY (db), graph.getWidth(), 1.0f);
    }

    // unity diagonal
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawLine (toX (lo), toY (lo), toX (hi), toY (hi), 1.0f);

    const auto p = currentParams();

    // threshold markers
    g.setColour (colour.withAlpha (0.25f));
    g.fillRect (toX (p.upThresholdDb), graph.getY(), 1.0f, graph.getHeight());
    g.fillRect (toX (p.downThresholdDb), graph.getY(), 1.0f, graph.getHeight());

    juce::Path curve;
    for (int i = 0; i <= 96; ++i)
    {
        const float in = lo + (hi - lo) * (float) i / 96.0f;
        const float out = in + st::dsp::UpDownCompressor::computeStaticGainDb (in, p);
        if (i == 0) curve.startNewSubPath (toX (in), toY (out));
        else curve.lineTo (toX (in), toY (out));
    }
    st::paint::glowStroke (g, curve, colour, 1.8f, 5.0f);

    // live operating point
    if (liveIn > lo && std::abs (p.depth) > 0.0005f)
    {
        const float out = liveIn + liveGain;
        auto dot = juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ toX (liveIn), toY (out) });
        g.setColour (colour.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (4.0f));
        g.setColour (t.text);
        g.fillEllipse (dot);
    }
    g.restoreState();

    if (std::abs (p.depth) < 0.0005f)
    {
        g.setColour (t.textFaint);
        g.setFont (st::fonts::medium (10.5f));
        g.drawFittedText ("Turn up Dynamics\nto engage", graph.reduced (8.0f).toNearestInt(), juce::Justification::centred, 2);
    }
}

void DynamicsPanel::resized()
{
    auto r = getLocalBounds();
    auto titleRow = r.removeFromTop (titleHeight + 6).reduced (14, 6).withTrimmedTop (2);
    position->setBounds (titleRow.removeFromRight (100));

    auto content = r.reduced (14, 0).withTrimmedBottom (12).withTrimmedTop (6);
    auto left = content.removeFromLeft (juce::jmin (content.getHeight() - 14, 150));
    meter->setBounds (left.removeFromBottom (6));
    left.removeFromBottom (6);
    graph = left.toFloat();

    content.removeFromLeft (10);
    const int rowH = content.getHeight() / 2;
    auto row1 = content.removeFromTop (rowH);
    auto row2 = content;
    const int w = row1.getWidth() / 3;
    upT->setBounds (row1.removeFromLeft (w));
    upR->setBounds (row1.removeFromLeft (w));
    attack->setBounds (row1);
    downT->setBounds (row2.removeFromLeft (w));
    downR->setBounds (row2.removeFromLeft (w));
    release->setBounds (row2);
}

//==============================================================================
MasterPanel::MasterPanel (StSatProcessor& p)
    : Card ("Master"),
      input (p.param (ids::inputGain), &p.undoManager, "Input"),
      output (p.param (ids::outputGain), &p.undoManager, "Output"),
      mix (p.param (ids::mix), &p.undoManager, "Mix"),
      pan (p.param (ids::outputPan), &p.undoManager, "Pan"),
      inMeter (p.inputMeter, "In"),
      outMeter (p.outputMeter, "Out")
{
    for (auto* k : { &input, &output, &mix, &pan })
    {
        k->setSize (st::Knob::Size::small);
        k->setColour (st::Theme::get().text.withAlpha (0.85f));
        addAndMakeVisible (*k);
    }
    input.setBipolar (true);
    output.setBipolar (true);
    pan.setBipolar (true);
    input.setTooltip ("Input gain into the whole plugin.");
    output.setTooltip ("Output gain.");
    mix.setTooltip ("Global dry/wet (latency compensated).");
    pan.setTooltip ("Output balance.");

    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
}

void MasterPanel::resized()
{
    auto r = getContentBounds();
    auto meters = r.removeFromRight (54);
    inMeter.setBounds (meters.removeFromLeft (24));
    meters.removeFromLeft (6);
    outMeter.setBounds (meters.removeFromLeft (24));
    r.removeFromRight (8);

    const int h = r.getHeight() / 2;
    auto row1 = r.removeFromTop (h), row2 = r;
    input.setBounds (row1.removeFromLeft (row1.getWidth() / 2));
    output.setBounds (row1);
    mix.setBounds (row2.removeFromLeft (row2.getWidth() / 2));
    pan.setBounds (row2);
}

} // namespace stsat
