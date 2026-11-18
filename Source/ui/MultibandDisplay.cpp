#include "MultibandDisplay.h"

namespace stsat
{

namespace
{
    st::SpectrumAnalyzer::Options analyzerOptions()
    {
        st::SpectrumAnalyzer::Options o;
        o.fftOrder = 13;
        o.numDisplayPoints = 320;
        o.minHz = 15.0f;
        o.maxHz = 24000.0f;
        o.releaseDbPerSec = 30.0f;
        o.smoothingPasses = 4;
        return o;
    }

    constexpr float specMinDb = -90.0f, specMaxDb = 0.0f;
    constexpr int hillResolution = 220;
}

MultibandDisplay::MultibandDisplay (StSatProcessor& p)
    : proc (p), preSpectrum (p.preAnalyzer, analyzerOptions()), postSpectrum (p.postAnalyzer, analyzerOptions())
{
    setOpaque (false);

    for (int b = 0; b < kMaxBands; ++b)
    {
        const float drive = proc.apvts.getRawParameterValue (ids::band (b, "drive"))->load();
        hillHeight[(size_t) b].snap (drive);
        presence[(size_t) b].snap (b < proc.getNumBands() ? 1.0f : 0.0f);
    }

    clock = std::make_unique<st::FrameClock> (*this, [this] (double dt) { frame (dt); });
}

MultibandDisplay::~MultibandDisplay() = default;

void MultibandDisplay::setAnalyzerMode (AnalyzerMode m)
{
    analyzerMode = m;
    proc.preAnalyzer.setActive (m == AnalyzerMode::preAndPost);
    proc.postAnalyzer.setActive (m != AnalyzerMode::off);
    if (m == AnalyzerMode::off) { preSpectrum.clear(); postSpectrum.clear(); }
    repaint();
}

//==============================================================================
float MultibandDisplay::xForHz (float hz) const noexcept
{
    return plot.getX() + plot.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
}

float MultibandDisplay::hzForX (float x) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return minHz * std::pow (maxHz / minHz, t);
}

// Hill height in pixels for a drive value. Even 0 dB keeps a visible hill above the panel.
float MultibandDisplay::heightForDrive (float db) const noexcept
{
    const float floorTop = panelArea.isEmpty() ? plot.getBottom() - 40.0f : panelArea.getY() - 26.0f;
    const float minH = baseline() - floorTop;
    const float maxH = baseline() - (plot.getY() + 64.0f);
    const float t = std::sqrt (juce::jlimit (0.0f, 1.0f, db / 48.0f));   // more travel at low drive
    return minH + (maxH - minH) * t;
}

float MultibandDisplay::driveForHeight (float h) const noexcept
{
    const float floorTop = panelArea.isEmpty() ? plot.getBottom() - 40.0f : panelArea.getY() - 26.0f;
    const float minH = baseline() - floorTop;
    const float maxH = baseline() - (plot.getY() + 64.0f);
    const float t = juce::jlimit (0.0f, 1.0f, (h - minH) / juce::jmax (1.0f, maxH - minH));
    return 48.0f * t * t;
}

float MultibandDisplay::crossover (int i) const
{
    return proc.apvts.getRawParameterValue (ids::crossover (i))->load();
}

float MultibandDisplay::bandMagnitude (int band, float hz) const
{
    const int nb = numBands();
    if (nb <= 1) return 1.0f;

    const int slopeIdx = (int) proc.apvts.getRawParameterValue (ids::slope)->load();
    const float order = slopeIdx == 0 ? 2.0f : (slopeIdx == 1 ? 4.0f : 8.0f);

    auto lp = [order] (float f, float fc) { const float r = std::pow (f / fc, order); return 1.0f / (1.0f + r); };
    auto hp = [order] (float f, float fc) { const float r = std::pow (f / fc, order); return r / (1.0f + r); };

    float m = 1.0f;
    if (band > 0)      m *= hp (hz, crossover (band - 1));
    if (band < nb - 1) m *= lp (hz, crossover (band));
    return m;
}

float MultibandDisplay::bandCentreHz (int b) const
{
    const int nb = numBands();
    const float lo = b == 0 ? minHz * 1.6f : crossover (b - 1);
    const float hi = b == nb - 1 ? maxHz / 1.6f : crossover (b);
    return std::sqrt (lo * hi);
}

float MultibandDisplay::getBandCentreX (int band) const
{
    return xForHz (bandCentreHz (juce::jlimit (0, juce::jmax (0, numBands() - 1), band)));
}

int MultibandDisplay::bandAtX (float x) const
{
    const int nb = numBands();
    const float hz = hzForX (x);
    for (int b = 0; b < nb - 1; ++b)
        if (hz < crossover (b)) return b;
    return nb - 1;
}

juce::Point<float> MultibandDisplay::driveHandle (int b) const
{
    const float x = xForHz (bandCentreHz (b));
    const float h = heightForDrive (hillHeight[(size_t) b].current) * bandMagnitude (b, bandCentreHz (b));
    return { x, baseline() - h };
}

juce::Rectangle<float> MultibandDisplay::labelBounds (int b) const
{
    const auto h = driveHandle (b);
    return { h.x - 70.0f, h.y - 40.0f, 140.0f, 18.0f };
}

juce::Rectangle<float> MultibandDisplay::removeBounds (int b) const
{
    const auto lb = labelBounds (b);
    const auto* name = getStyleInfo ((int) proc.apvts.getRawParameterValue (ids::band (b, "style"))->load()).name;
    const float w = st::fonts::stringWidth (st::fonts::semibold (11.5f), name);
    return juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ lb.getCentreX() + w * 0.5f + 16.0f, lb.getCentreY() });
}

juce::Point<float> MultibandDisplay::crossoverHandle (int i) const
{
    const float f = crossover (i);
    const float hA = heightForDrive (hillHeight[(size_t) i].current) * bandMagnitude (i, f);
    const float hB = heightForDrive (hillHeight[(size_t) i + 1].current) * bandMagnitude (i + 1, f);
    return { xForHz (f), baseline() - juce::jmax (hA, hB) };
}

MultibandDisplay::Hit MultibandDisplay::hitTestAt (juce::Point<float> p) const
{
    if (! plot.contains (p)) return {};
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
    {
        if (nb > 1 && (hover.index == b || selected == b) && removeBounds (b).expanded (3.0f).contains (p))
            return { Target::removeButton, b };
        if (driveHandle (b).getDistanceFrom (p) < 13.0f)
            return { Target::driveHandle, b };
    }

    for (int i = 0; i < nb - 1; ++i)
    {
        const auto h = crossoverHandle (i);
        if (std::abs (h.x - p.x) < 8.0f && p.y > h.y - 16.0f)
            return { Target::crossover, i };
    }

    return { Target::bandBody, bandAtX (p.x) };
}

//==============================================================================
void MultibandDisplay::resized()
{
    plot = getLocalBounds().toFloat();
}

void MultibandDisplay::frame (double dt)
{
    if (analyzerMode != AnalyzerMode::off)
    {
        postSpectrum.update (dt);
        if (analyzerMode == AnalyzerMode::preAndPost) preSpectrum.update (dt);
    }

    const int nb = numBands();
    for (int b = 0; b < kMaxBands; ++b)
    {
        auto i = (size_t) b;
        hillHeight[i].setTarget (proc.apvts.getRawParameterValue (ids::band (b, "drive"))->load());
        hillHeight[i].tick (dt, drag.index == b && drag.target != Target::crossover ? 40.0f : 16.0f);

        presence[i].setTarget (b < nb ? 1.0f : 0.0f);
        presence[i].tick (dt, 12.0f);

        const bool hov = hover.index == b && hover.target != Target::crossover && hover.target != Target::none;
        hoverAmt[i].setTarget (hov ? 1.0f : 0.0f);
        hoverAmt[i].tick (dt, 18.0f);

        selectAmt[i].setTarget (b == selected ? 1.0f : 0.0f);
        selectAmt[i].tick (dt, 12.0f);

        const float lvl = proc.getEngine().getBand (b).outputLevelDb.load();
        glow[i].setTarget (juce::jlimit (0.0f, 1.0f, (lvl + 42.0f) / 42.0f));
        glow[i].tick (dt, glow[i].target > glow[i].current ? 30.0f : 6.0f);
    }

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const bool on = (hover.target == Target::crossover && hover.index == i) || (drag.target == Target::crossover && drag.index == i);
        xoverHover[(size_t) i].setTarget (on ? 1.0f : 0.0f);
        xoverHover[(size_t) i].tick (dt, 18.0f);
    }

    hintAlpha.setTarget (nb == 1 && hover.target == Target::bandBody ? 1.0f : 0.0f);
    hintAlpha.tick (dt, 8.0f);

    // Output meter
    {
        const auto r = proc.outputMeter.read();
        for (size_t c = 0; c < 2; ++c)
        {
            const float db = juce::Decibels::gainToDecibels (r.peak[c], -100.0f);
            meterDb[c] = db > meterDb[c] ? db : juce::jmax (db, meterDb[c] - 30.0f * (float) dt);
            meterHold[c] = db > meterHold[c] ? db : meterHold[c] - 12.0f * (float) dt;
        }
    }

    if (onFrame) onFrame (dt);
    repaint();
}

//==============================================================================
void MultibandDisplay::paint (juce::Graphics& g)
{
    drawBackground (g);
    drawSpectrum (g);
    drawHills (g);
    drawCrossovers (g);
    drawLabels (g);
    drawMeter (g);

    // Dissolve the bottom edge into the backdrop so the display has no hard border.
    const auto& t = st::Theme::get();
    auto fade = plot.withTop (plot.getBottom() - 70.0f);
    g.setGradientFill (juce::ColourGradient (t.bg0.withAlpha (0.0f), 0.0f, fade.getY(), t.bg0, 0.0f, fade.getBottom(), false));
    g.fillRect (fade);
}

void MultibandDisplay::drawBackground (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    // Deep gradient that dissolves into the editor backdrop at the bottom (no hard edges).
    juce::ColourGradient bg (t.bg0.darker (0.35f), plot.getCentreX(), plot.getY(),
                             t.bg0.withAlpha (0.0f), plot.getCentreX(), plot.getBottom(), false);
    bg.addColour (0.75, t.bg0.darker (0.2f).withAlpha (0.85f));
    g.setGradientFill (bg);
    g.fillRect (plot);

    // Frequency grid: hairlines that fade towards the bottom.
    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int m = 1; m <= 9; ++m)
        {
            const float f = decade * (float) m;
            if (f < minHz || f > maxHz) continue;
            const float x = xForHz (f);
            const bool major = (m == 1);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (major ? 0.05f : 0.022f), x, plot.getY(),
                                                     juce::Colours::white.withAlpha (0.0f), x, plot.getBottom() - 40.0f, false));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
        }

    // Labels along the top, discreet
    g.setFont (st::fonts::medium (10.0f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (f);
        const auto txt = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        g.setColour (t.textFaint.withAlpha (0.8f));
        g.drawText (txt, juce::Rectangle<float> (x + 4.0f, plot.getY() + 8.0f, 40.0f, 12.0f), juce::Justification::centredLeft, false);
    }
}

void MultibandDisplay::drawSpectrum (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off) return;
    const auto& t = st::Theme::get();
    auto area = plot.withTrimmedTop (28.0f);

    if (analyzerMode == AnalyzerMode::preAndPost)
    {
        auto pre = preSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
        g.setColour (t.textDim.withAlpha (0.28f));
        g.strokePath (pre, juce::PathStrokeType (1.0f));
    }

    auto fill = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, true);
    g.setGradientFill (juce::ColourGradient (t.text.withAlpha (0.085f), 0.0f, area.getY() + area.getHeight() * 0.3f,
                                             t.text.withAlpha (0.0f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);

    auto line = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
    g.setColour (t.text.withAlpha (0.30f));
    g.strokePath (line, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved));
}

void MultibandDisplay::drawHills (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    // Unselected first, selected last (on top)
    std::vector<int> order;
    for (int b = 0; b < nb; ++b)
        if (b != selected) order.push_back (b);
    if (selected < nb) order.push_back (selected);

    for (int b : order)
    {
        const auto i = (size_t) b;
        const auto colour = t.seriesColour (b);
        const float pres = presence[i].current;
        const float sel = selectAmt[i].current, hov = hoverAmt[i].current;

        const bool bypassed = proc.apvts.getRawParameterValue (ids::band (b, "bypass"))->load() > 0.5f;
        const bool muted = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;
        const float activity = (bypassed || muted) ? 0.35f : 1.0f;

        const float h = heightForDrive (hillHeight[i].current) * pres;

        juce::Path stroke, fill;
        fill.startNewSubPath (plot.getX(), baseline());
        for (int k = 0; k <= hillResolution; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) hillResolution;
            const float y = baseline() - h * bandMagnitude (b, hzForX (x));
            if (k == 0) stroke.startNewSubPath (x, y); else stroke.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (plot.getRight(), baseline());
        fill.closeSubPath();

        const float top = baseline() - h;
        const float fillAlpha = (0.10f + 0.16f * sel + 0.06f * hov + 0.08f * glow[i].current) * activity;
        juce::ColourGradient grad (colour.withAlpha (fillAlpha), 0.0f, top,
                                   colour.withAlpha (fillAlpha * 0.15f), 0.0f, baseline(), false);
        g.setGradientFill (grad);
        g.fillPath (fill);

        const float lineAlpha = (0.45f + 0.5f * sel + 0.2f * hov) * activity;
        st::paint::glowStroke (g, stroke, colour.withAlpha (juce::jmin (1.0f, lineAlpha)), 1.4f + 0.8f * sel, 5.0f + 5.0f * sel);

        // Drive handle
        const auto hp = driveHandle (b);
        const float r = 5.5f + 2.0f * sel + 1.5f * hov;
        g.setColour (colour.withAlpha (0.16f + 0.14f * sel));
        g.fillEllipse (juce::Rectangle<float> (r * 3.4f, r * 3.4f).withCentre (hp));
        g.setColour (t.bg0);
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (hp));
        g.setColour (colour.withAlpha (activity));
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (hp), 2.0f);
        g.fillEllipse (juce::Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (hp));
    }
}

void MultibandDisplay::drawCrossovers (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int i = 0; i < nb - 1; ++i)
    {
        const float a = xoverHover[(size_t) i].current;
        const auto h = crossoverHandle (i);

        // A soft vertical light that only really appears on hover/drag
        g.setGradientFill (juce::ColourGradient (t.text.withAlpha (0.04f + 0.30f * a), h.x, h.y,
                                                 t.text.withAlpha (0.0f), h.x, baseline(), false));
        g.fillRect (h.x - 0.5f, h.y, 1.0f, baseline() - h.y);

        // Capsule grip sitting where the hills cross
        auto grip = juce::Rectangle<float> (6.0f + 2.0f * a, 18.0f + 4.0f * a).withCentre ({ h.x, h.y - 2.0f });
        g.setColour (t.bg0.withAlpha (0.85f));
        g.fillRoundedRectangle (grip.expanded (2.0f), grip.getWidth());
        g.setColour (t.text.withAlpha (0.35f + 0.6f * a));
        g.fillRoundedRectangle (grip, grip.getWidth() * 0.5f);

        if (a > 0.02f)
        {
            auto txt = st::params::formatHz (crossover (i));
            g.setFont (st::fonts::semibold (11.0f));
            const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 16.0f;
            auto bubble = juce::Rectangle<float> (w, 20.0f).withCentre ({ h.x, h.y - 28.0f });
            g.setColour (t.bg3.withAlpha (0.92f * a));
            g.fillRoundedRectangle (bubble, 10.0f);
            g.setColour (t.text.withAlpha (a));
            g.drawText (txt, bubble, juce::Justification::centred, false);
        }
    }
}

void MultibandDisplay::drawLabels (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
    {
        const auto i = (size_t) b;
        const auto colour = t.seriesColour (b);
        const float sel = selectAmt[i].current, hov = hoverAmt[i].current;
        auto lb = labelBounds (b);

        const int style = (int) proc.apvts.getRawParameterValue (ids::band (b, "style"))->load();
        const bool dragging = drag.index == b && (drag.target == Target::driveHandle || drag.target == Target::bandBody);

        const juce::String text = dragging ? proc.bandParam (b, "drive").getCurrentValueAsText()
                                           : juce::String (getStyleInfo (style).name);

        g.setFont (st::fonts::semibold (11.5f));
        g.setColour (t.text.withAlpha (0.45f + 0.55f * juce::jmax (sel, hov)));
        g.drawText (text, lb, juce::Justification::centred, false);

        // Small coloured tick under the label
        g.setColour (colour.withAlpha (0.4f + 0.6f * sel));
        g.fillRoundedRectangle (juce::Rectangle<float> (14.0f + 10.0f * sel, 2.0f).withCentre ({ lb.getCentreX(), lb.getBottom() + 3.0f }), 1.0f);

        const bool bypassed = proc.apvts.getRawParameterValue (ids::band (b, "bypass"))->load() > 0.5f;
        const bool muted = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;
        const bool soloed = proc.apvts.getRawParameterValue (ids::band (b, "solo"))->load() > 0.5f;
        if (bypassed || muted || soloed)
        {
            g.setFont (st::fonts::caption (9.0f));
            g.setColour (soloed ? t.warning : t.textFaint);
            g.drawText (soloed ? "SOLO" : bypassed ? "OFF" : "MUTED", lb.translated (0.0f, -15.0f), juce::Justification::centred, false);
        }

        // Remove button fades in on hover / selection
        const float rm = nb > 1 ? juce::jmax (hov, sel * 0.6f) : 0.0f;
        if (rm > 0.02f && ! dragging)
        {
            auto rb = removeBounds (b);
            const bool over = hover.target == Target::removeButton && hover.index == b;
            g.setColour ((over ? t.bg4 : t.bg2).withAlpha (0.9f * rm));
            g.fillEllipse (rb);
            st::icons::draw (g, st::icons::Icon::close, rb.reduced (3.5f), (over ? t.text : t.textDim).withAlpha (rm), 1.6f);
        }
    }

    if (hintAlpha.current > 0.01f)
    {
        g.setFont (st::fonts::medium (12.0f));
        g.setColour (t.textDim.withAlpha (0.8f * hintAlpha.current));
        const float y = panelArea.isEmpty() ? plot.getBottom() - 40.0f : panelArea.getY() - 70.0f;
        g.drawText ("Double-click anywhere to split into bands", juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), 18.0f),
                    juce::Justification::centred, false);
    }
}

void MultibandDisplay::drawMeter (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    auto area = juce::Rectangle<float> (plot.getRight() - 18.0f, plot.getY() + 30.0f, 10.0f,
                                        (panelArea.isEmpty() ? plot.getBottom() : panelArea.getY()) - plot.getY() - 60.0f);

    auto yFor = [&] (float db) { return area.getBottom() - area.getHeight() * std::pow (juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f), 1.4f); };

    for (int c = 0; c < 2; ++c)
    {
        auto bar = area.withWidth (4.0f).translated ((float) c * 6.0f, 0.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.fillRoundedRectangle (bar, 2.0f);

        const float y = yFor (meterDb[(size_t) c]);
        juce::ColourGradient grad (t.positive.withAlpha (0.8f), 0.0f, bar.getBottom(), t.danger, 0.0f, area.getY(), false);
        grad.addColour (0.8, t.warning);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bar.withTop (y), 2.0f);

        if (meterHold[(size_t) c] > -60.0f)
        {
            g.setColour (meterHold[(size_t) c] > 0.0f ? t.danger : t.text.withAlpha (0.7f));
            g.fillRect (bar.getX(), yFor (meterHold[(size_t) c]) - 0.5f, bar.getWidth(), 1.0f);
        }
    }

    const float peak = juce::jmax (meterHold[0], meterHold[1]);
    g.setFont (st::fonts::medium (9.5f));
    g.setColour (peak > 0.0f ? t.danger : t.textFaint);
    g.drawText (peak <= -60.0f ? juce::String ("-inf") : juce::String (peak, 1),
                juce::Rectangle<float> (area.getX() - 20.0f, area.getY() - 18.0f, 40.0f, 12.0f), juce::Justification::centred, false);
}

//==============================================================================
void MultibandDisplay::mouseMove (const juce::MouseEvent& e)
{
    auto h = hitTestAt (e.position);
    if (h.target != hover.target || h.index != hover.index)
    {
        hover = h;
        setMouseCursor (h.target == Target::crossover ? juce::MouseCursor::LeftRightResizeCursor
                      : h.target == Target::driveHandle ? juce::MouseCursor::UpDownResizeCursor
                      : h.target == Target::removeButton ? juce::MouseCursor::PointingHandCursor
                      : juce::MouseCursor::NormalCursor);
    }
}

void MultibandDisplay::mouseExit (const juce::MouseEvent&)
{
    hover = {};
}

void MultibandDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = hitTestAt (e.position);
    dragStart = e.position;
    dragParam = nullptr;

    if (drag.target == Target::none || drag.target == Target::removeButton)
        return;

    if (drag.target == Target::crossover)
    {
        proc.undoManager.beginNewTransaction ("Move crossover");
        dragParam = &proc.param (ids::crossover (drag.index));
        dragParam->beginChangeGesture();
        return;
    }

    if (selected != drag.index)
    {
        selected = drag.index;
        if (onBandSelected) onBandSelected (selected);
    }

    if (e.mods.isPopupMenu()) { drag = {}; return; }

    proc.undoManager.beginNewTransaction ("Drive");
    dragParam = &proc.bandParam (drag.index, "drive");
    dragStartValue = dragParam->convertFrom0to1 (dragParam->getValue());
    dragParam->beginChangeGesture();
}

void MultibandDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragParam == nullptr) return;

    if (drag.target == Target::crossover)
    {
        proc.setCrossoverOrdered (drag.index, hzForX (e.position.x), false);
        return;
    }

    float db;
    if (drag.target == Target::driveHandle)
    {
        const float mag = juce::jmax (0.3f, bandMagnitude (drag.index, bandCentreHz (drag.index)));
        db = driveForHeight ((baseline() - e.position.y) / mag);
    }
    else
    {
        const float sens = e.mods.isShiftDown() ? 0.04f : 0.18f;
        db = dragStartValue + (dragStart.y - e.position.y) * sens;
    }

    dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (juce::jlimit (0.0f, 48.0f, db)));
}

void MultibandDisplay::mouseUp (const juce::MouseEvent& e)
{
    if (drag.target == Target::removeButton)
    {
        const auto h = hitTestAt (e.position);
        if (h.target == Target::removeButton && h.index == drag.index)
        {
            proc.removeBand (drag.index);
            selected = juce::jmax (0, juce::jlimit (0, numBands() - 1, selected >= drag.index ? selected - 1 : selected));
            if (onBandSelected) onBandSelected (selected);
        }
    }

    if (dragParam != nullptr)
        dragParam->endChangeGesture();

    dragParam = nullptr;
    drag = {};
}

void MultibandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTestAt (e.position);

    if (h.target == Target::driveHandle)
    {
        auto& p = proc.bandParam (h.index, "drive");
        p.beginChangeGesture();
        p.setValueNotifyingHost (p.getDefaultValue());
        p.endChangeGesture();
        return;
    }

    if (h.target == Target::bandBody && numBands() < kMaxBands)
    {
        const int before = bandAtX (e.position.x);
        proc.addBand (hzForX (e.position.x));
        // The new band starts flat so it visibly grows into place.
        const int added = juce::jmin (before + 1, numBands() - 1);
        presence[(size_t) added].snap (0.0f);
        selected = added;
        if (onBandSelected) onBandSelected (selected);
    }
}

void MultibandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTestAt (e.position);
    if (h.target != Target::bandBody && h.target != Target::driveHandle) return;

    auto& p = proc.bandParam (h.index, "drive");
    const float dy = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    const float db = p.convertFrom0to1 (p.getValue()) + dy * (e.mods.isShiftDown() ? 1.0f : 4.0f) * (w.isReversed ? -1.0f : 1.0f);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (0.0f, 48.0f, db)));
    p.endChangeGesture();
}

} // namespace stsat
