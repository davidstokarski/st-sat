#include "MultibandDisplay.h"

namespace stsat
{

namespace
{
    st::SpectrumAnalyzer::Options analyzerOptions()
    {
        st::SpectrumAnalyzer::Options o;
        o.fftOrder = 13;
        o.numDisplayPoints = 340;
        o.minHz = 15.0f;
        o.maxHz = 24000.0f;
        o.releaseDbPerSec = 28.0f;
        o.smoothingPasses = 2;
        return o;
    }

    constexpr float specMinDb = -84.0f, specMaxDb = 0.0f;
}

MultibandDisplay::MultibandDisplay (StSatProcessor& p)
    : proc (p), preSpectrum (p.preAnalyzer, analyzerOptions()), postSpectrum (p.postAnalyzer, analyzerOptions())
{
    setOpaque (false);
    for (int i = 0; i < kMaxCrossovers; ++i)
        xoverPos[(size_t) i].snap (crossover (i));
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

float MultibandDisplay::yForDrive (float db) const noexcept
{
    const float t = std::sqrt (juce::jlimit (0.0f, 1.0f, db / 48.0f));
    return handleBottom + (handleTop - handleBottom) * t;
}

float MultibandDisplay::driveForY (float y) const noexcept
{
    const float t = juce::jlimit (0.0f, 1.0f, (handleBottom - y) / juce::jmax (1.0f, handleBottom - handleTop));
    return 48.0f * t * t;
}

float MultibandDisplay::crossover (int i) const
{
    return proc.apvts.getRawParameterValue (ids::crossover (i))->load();
}

juce::Range<float> MultibandDisplay::getBandXRange (int b) const
{
    const int nb = numBands();
    const float x0 = b == 0 ? plot.getX() : xForHz (xoverPos[(size_t) b - 1].current);
    const float x1 = b >= nb - 1 ? plot.getRight() : xForHz (xoverPos[(size_t) b].current);
    return { x0, juce::jmax (x0 + 1.0f, x1) };
}

float MultibandDisplay::getBandCentreX (int band) const
{
    return getBandXRange (juce::jlimit (0, juce::jmax (0, numBands() - 1), band)).getStart()
         + getBandXRange (juce::jlimit (0, juce::jmax (0, numBands() - 1), band)).getLength() * 0.5f;
}

int MultibandDisplay::bandAtX (float x) const
{
    const int nb = numBands();
    for (int b = 0; b < nb - 1; ++b)
        if (x < getBandXRange (b).getEnd()) return b;
    return nb - 1;
}

juce::Rectangle<float> MultibandDisplay::handleBounds (int b) const
{
    return juce::Rectangle<float> (60.0f, 24.0f).withCentre ({ getBandCentreX (b), handleY[(size_t) b].current });
}

juce::Rectangle<float> MultibandDisplay::gripBounds (int i) const
{
    const float x = xForHz (xoverPos[(size_t) i].current);
    const float y = handleTop + (handleBottom - handleTop) * 0.42f;
    return juce::Rectangle<float> (26.0f, 30.0f).withCentre ({ x, y });
}

MultibandDisplay::Hit MultibandDisplay::hitTestAt (juce::Point<float> p) const
{
    if (! plot.contains (p)) return {};
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
        if (handleBounds (b).expanded (4.0f).contains (p))
            return { Target::handle, b };

    for (int i = 0; i < nb - 1; ++i)
    {
        const auto g = gripBounds (i);
        if (g.expanded (3.0f).contains (p) || std::abs (g.getCentreX() - p.x) < 5.0f)
            return { Target::crossover, i };
    }

    return { Target::bandBody, bandAtX (p.x) };
}

//==============================================================================
void MultibandDisplay::resized()
{
    plot = getLocalBounds().toFloat();
    for (int b = 0; b < kMaxBands; ++b)
        handleY[(size_t) b].snap (yForDrive (proc.apvts.getRawParameterValue (ids::band (b, "drive"))->load()));
}

void MultibandDisplay::frame (double dt)
{
    if (analyzerMode != AnalyzerMode::off)
    {
        postSpectrum.update (dt);
        if (analyzerMode == AnalyzerMode::preAndPost) preSpectrum.update (dt);
    }

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto i = (size_t) b;
        handleY[i].setTarget (yForDrive (proc.apvts.getRawParameterValue (ids::band (b, "drive"))->load()));
        handleY[i].tick (dt, drag.index == b && drag.target == Target::handle ? 60.0f : 18.0f);

        const bool hov = hover.index == b && (hover.target == Target::handle || hover.target == Target::bandBody);
        hoverAmt[i].setTarget (hov ? 1.0f : 0.0f);
        hoverAmt[i].tick (dt, 16.0f);
        selectAmt[i].setTarget (b == selected ? 1.0f : 0.0f);
        selectAmt[i].tick (dt, 10.0f);
    }

    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const bool on = (hover.target == Target::crossover && hover.index == i) || (drag.target == Target::crossover && drag.index == i);
        xoverHover[(size_t) i].setTarget (on ? 1.0f : 0.0f);
        xoverHover[(size_t) i].tick (dt, 18.0f);
        xoverPos[(size_t) i].setTarget (crossover (i));
        xoverPos[(size_t) i].tick (dt, drag.target == Target::crossover ? 80.0f : 20.0f);
    }

    hintAlpha.setTarget (numBands() == 1 && hover.target == Target::bandBody ? 1.0f : 0.0f);
    hintAlpha.tick (dt, 8.0f);

    if (onFrame) onFrame (dt);
    repaint();
}

//==============================================================================
void MultibandDisplay::paint (juce::Graphics& g)
{
    drawColumns (g);
    drawSpectrum (g);
    drawCrossovers (g);
    drawHandles (g);
    drawModDots (g);

    if (hintAlpha.current > 0.01f)
    {
        const auto& t = st::Theme::get();
        g.setFont (st::fonts::medium (12.5f));
        g.setColour (t.text.withAlpha (0.55f * hintAlpha.current));
        g.drawText ("Double-click to add a band", juce::Rectangle<float> (plot.getX(), handleTop - 34.0f, plot.getWidth(), 18.0f),
                    juce::Justification::centred, false);
    }
}

void MultibandDisplay::drawColumns (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
    {
        const auto i = (size_t) b;
        const float sel = selectAmt[i].current, hov = hoverAmt[i].current;
        if (sel + hov < 0.01f) continue;

        const auto xr = getBandXRange (b);
        auto col = juce::Rectangle<float> (xr.getStart(), plot.getY(), xr.getLength(), plot.getHeight());

        // Red light pouring down from the top of the selected band
        juce::ColourGradient grad (t.displayGlow.withAlpha (0.42f * sel + 0.10f * hov), 0.0f, plot.getY(),
                                   t.displayGlow.withAlpha (0.0f), 0.0f, plot.getBottom(), false);
        grad.addColour (0.55, t.displayGlow.withAlpha (0.22f * sel + 0.05f * hov));
        g.setGradientFill (grad);
        g.fillRect (col);

        // Soft edges so the column blends into neighbours
        for (int side = 0; side < 2; ++side)
        {
            const float x = side == 0 ? col.getX() : col.getRight();
            juce::ColourGradient edge (juce::Colours::white.withAlpha (0.05f * sel), x, plot.getY(),
                                       juce::Colours::white.withAlpha (0.0f), x, plot.getBottom() * 0.8f, false);
            g.setGradientFill (edge);
            g.fillRect (x - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        }
    }
}

void MultibandDisplay::drawSpectrum (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off) return;
    const auto& t = st::Theme::get();
    auto area = plot.withTrimmedTop (60.0f).withBottom (handleBottom + 40.0f);

    if (analyzerMode == AnalyzerMode::preAndPost)
    {
        auto pre = preSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
        g.setColour (t.textDim.withAlpha (0.22f));
        g.strokePath (pre, juce::PathStrokeType (1.0f));
    }

    auto fill = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, true);
    g.setGradientFill (juce::ColourGradient (t.spectrum.withAlpha (0.07f), 0.0f, area.getY(),
                                             t.spectrum.withAlpha (0.0f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);

    auto line = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
    g.setColour (t.spectrum.withAlpha (0.62f));
    g.strokePath (line, juce::PathStrokeType (1.25f, juce::PathStrokeType::curved));
}

void MultibandDisplay::drawCrossovers (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int i = 0; i < nb - 1; ++i)
    {
        const float a = xoverHover[(size_t) i].current;
        const auto grip = gripBounds (i);
        const float x = grip.getCentreX();

        // dashed divider
        juce::Path line;
        line.startNewSubPath (x, plot.getY() + 58.0f);
        line.lineTo (x, handleBottom + 18.0f);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.30f + 0.35f * a));
        g.fillPath (dashed);

        // grip button with a little "tent" glyph
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (grip.translated (0.0f, 1.5f), 4.0f);
        g.setColour (juce::Colour (0xff3b3238).withAlpha (0.92f).brighter (0.25f * a));
        g.fillRoundedRectangle (grip, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawRoundedRectangle (grip.reduced (0.5f), 4.0f, 1.0f);

        juce::Path tent;
        const auto c = grip.getCentre();
        tent.startNewSubPath (c.x - 7.0f, c.y + 6.0f);
        tent.quadraticTo (c.x - 3.0f, c.y + 5.0f, c.x, c.y - 7.0f);
        tent.quadraticTo (c.x + 3.0f, c.y + 5.0f, c.x + 7.0f, c.y + 6.0f);
        tent.startNewSubPath (c.x - 8.0f, c.y - 7.0f);
        tent.lineTo (c.x + 8.0f, c.y - 7.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f + 0.25f * a));
        g.strokePath (tent, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        if (a > 0.02f)
        {
            auto txt = st::params::formatHz (crossover (i));
            g.setFont (st::fonts::semibold (11.5f));
            const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 16.0f;
            auto bubble = juce::Rectangle<float> (w, 20.0f).withCentre ({ x, grip.getY() - 18.0f });
            g.setColour (juce::Colour (0xff1a1416).withAlpha (0.9f * a));
            g.fillRoundedRectangle (bubble, 4.0f);
            g.setColour (t.text.withAlpha (a));
            g.drawText (txt, bubble, juce::Justification::centred, false);
        }
    }
}

void MultibandDisplay::drawHandles (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
    {
        const auto i = (size_t) b;
        const float sel = selectAmt[i].current, hov = hoverAmt[i].current;
        const auto xr = getBandXRange (b);
        const float y = handleY[i].current;

        const bool bypassed = proc.apvts.getRawParameterValue (ids::band (b, "bypass"))->load() > 0.5f;
        const bool muted = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;

        // Level line across the band
        g.setColour (juce::Colours::white.withAlpha ((bypassed || muted ? 0.08f : 0.16f) + 0.18f * sel + 0.08f * hov));
        g.fillRect (xr.getStart() + 1.0f, y - 0.5f, xr.getLength() - 2.0f, 1.0f);

        // Tint below the line in the selected band (subtle lift)
        if (sel > 0.01f)
        {
            g.setGradientFill (juce::ColourGradient (t.displayGlow.withAlpha (0.18f * sel), 0.0f, y,
                                                     t.displayGlow.withAlpha (0.0f), 0.0f, y + 120.0f, false));
            g.fillRect (xr.getStart() + 1.0f, y, xr.getLength() - 2.0f, 120.0f);
        }

        auto hb = handleBounds (b);
        const bool active = drag.index == b && drag.target == Target::handle;
        st::paint::pillHandle (g, hb, hov > 0.5f, active);

        // Small caption under the handle: style name, or value while dragging
        const bool showValue = active || (drag.index == b && drag.target == Target::bandBody);
        const float captionAlpha = juce::jmax (sel * 0.8f, hov, showValue ? 1.0f : 0.0f);
        if (captionAlpha > 0.02f)
        {
            const int style = (int) proc.apvts.getRawParameterValue (ids::band (b, "style"))->load();
            const auto txt = showValue ? "Drive " + proc.bandParam (b, "drive").getCurrentValueAsText()
                                       : juce::String (getStyleInfo (style).name);
            g.setFont (st::fonts::medium (11.0f));
            g.setColour (t.text.withAlpha (0.75f * captionAlpha));
            g.drawText (txt, hb.withSizeKeepingCentre (180.0f, 16.0f).translated (0.0f, 22.0f), juce::Justification::centred, false);
        }

        if (bypassed || muted)
        {
            g.setFont (st::fonts::caption (9.0f));
            g.setColour (t.textFaint);
            g.drawText (bypassed ? "OFF" : "MUTED", hb.withSizeKeepingCentre (80.0f, 14.0f).translated (0.0f, -22.0f), juce::Justification::centred, false);
        }
    }
}

void MultibandDisplay::drawModDots (juce::Graphics& g)
{
    // One dot per routed modulation source, along the top of the band it affects (Saturn-style).
    const int nb = numBands();
    const auto routings = proc.modMatrix.getRoutings();

    for (int b = 0; b < nb; ++b)
    {
        const auto prefix = "b" + juce::String (b + 1) + "_";
        juce::Array<int> sources;
        for (auto& r : routings)
            if (r.targetId.startsWith (prefix))
                sources.addIfNotAlreadyThere (proc.modMatrix.getSourceIndex (r.source));

        if (sources.isEmpty()) continue;
        const float x0 = getBandCentreX (b) - (float) (sources.size() - 1) * 5.0f;
        for (int k = 0; k < sources.size(); ++k)
        {
            const auto c = proc.modMatrix.getSourceColour (sources[k]);
            const juce::Point<float> p (x0 + (float) k * 10.0f, plot.getY() + 76.0f);
            g.setColour (c.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (p));
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
        }
    }
}

//==============================================================================
void MultibandDisplay::mouseMove (const juce::MouseEvent& e)
{
    auto h = hitTestAt (e.position);
    if (h.target != hover.target || h.index != hover.index)
    {
        hover = h;
        setMouseCursor (h.target == Target::crossover ? juce::MouseCursor::LeftRightResizeCursor
                      : h.target == Target::handle ? juce::MouseCursor::UpDownResizeCursor
                      : juce::MouseCursor::NormalCursor);
    }
}

void MultibandDisplay::mouseExit (const juce::MouseEvent&) { hover = {}; }

void MultibandDisplay::mouseDown (const juce::MouseEvent& e)
{
    drag = hitTestAt (e.position);
    dragStart = e.position;
    dragParam = nullptr;
    if (drag.target == Target::none) return;

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
    if (drag.target == Target::handle)
    {
        const float startY = yForDrive (dragStartValue);
        const float scale = e.mods.isShiftDown() ? 0.2f : 1.0f;
        db = driveForY (startY + (e.position.y - dragStart.y) * scale);
    }
    else
        db = dragStartValue + (dragStart.y - e.position.y) * (e.mods.isShiftDown() ? 0.04f : 0.18f);

    dragParam->setValueNotifyingHost (dragParam->convertTo0to1 (juce::jlimit (0.0f, 48.0f, db)));
}

void MultibandDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragParam != nullptr) dragParam->endChangeGesture();
    dragParam = nullptr;
    drag = {};
}

void MultibandDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTestAt (e.position);

    if (h.target == Target::handle)
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
        selected = juce::jmin (before + 1, numBands() - 1);
        for (int i = 0; i < kMaxCrossovers; ++i) xoverPos[(size_t) i].snap (crossover (i));
        if (onBandSelected) onBandSelected (selected);
    }
}

void MultibandDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTestAt (e.position);
    if (h.target != Target::bandBody && h.target != Target::handle) return;

    auto& p = proc.bandParam (h.index, "drive");
    const float dy = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
    const float db = p.convertFrom0to1 (p.getValue()) + dy * (e.mods.isShiftDown() ? 1.0f : 4.0f) * (w.isReversed ? -1.0f : 1.0f);
    p.beginChangeGesture();
    p.setValueNotifyingHost (p.convertTo0to1 (juce::jlimit (0.0f, 48.0f, db)));
    p.endChangeGesture();
}

} // namespace stsat
