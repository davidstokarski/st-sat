#include "MultibandDisplay.h"

namespace stsat
{

namespace
{
    st::SpectrumAnalyzer::Options analyzerOptions()
    {
        st::SpectrumAnalyzer::Options o;
        o.fftOrder = 13;
        o.numDisplayPoints = 300;
        o.minHz = 15.0f;
        o.maxHz = 24000.0f;
        o.releaseDbPerSec = 36.0f;
        return o;
    }

    constexpr float minDriveDb = 0.0f, maxDriveDb = 48.0f;
    constexpr float specMinDb = -90.0f, specMaxDb = 6.0f;
}

MultibandDisplay::MultibandDisplay (StSatProcessor& p)
    : proc (p), preSpectrum (p.preAnalyzer, analyzerOptions()), postSpectrum (p.postAnalyzer, analyzerOptions())
{
    setOpaque (false);
    startTimerHz (60);
    lastTick = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

MultibandDisplay::~MultibandDisplay() { stopTimer(); }

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
    const float top = plot.getY() + 46.0f, bottom = plot.getBottom() - 18.0f;
    return juce::jmap (juce::jlimit (minDriveDb, maxDriveDb, db), minDriveDb, maxDriveDb, bottom, top);
}

float MultibandDisplay::driveForY (float y) const noexcept
{
    const float top = plot.getY() + 46.0f, bottom = plot.getBottom() - 18.0f;
    return juce::jmap (juce::jlimit (top, bottom, y), bottom, top, minDriveDb, maxDriveDb);
}

float MultibandDisplay::crossover (int i) const
{
    return proc.apvts.getRawParameterValue (ids::crossover (i))->load();
}

juce::Range<float> MultibandDisplay::bandXRange (int b) const
{
    const int nb = numBands();
    const float x0 = b == 0 ? plot.getX() : xForHz (crossover (b - 1));
    const float x1 = b == nb - 1 ? plot.getRight() : xForHz (crossover (b));
    return { x0, juce::jmax (x0 + 1.0f, x1) };
}

int MultibandDisplay::bandAtX (float x) const
{
    const int nb = numBands();
    for (int b = 0; b < nb; ++b)
        if (x < bandXRange (b).getEnd()) return b;
    return nb - 1;
}

juce::Rectangle<float> MultibandDisplay::pillBounds (int b) const
{
    const auto r = bandXRange (b);
    const float w = juce::jlimit (28.0f, 150.0f, r.getLength() - 12.0f);
    return { r.getStart() + (r.getLength() - w) * 0.5f, plot.getY() + 10.0f, w, 24.0f };
}

juce::Rectangle<float> MultibandDisplay::removeBounds (int b) const
{
    auto p = pillBounds (b);
    return p.removeFromRight (22.0f).reduced (3.0f);
}

juce::Point<float> MultibandDisplay::driveHandle (int b) const
{
    const auto r = bandXRange (b);
    const float drive = proc.apvts.getRawParameterValue (ids::band (b, "drive"))->load();
    return { r.getStart() + r.getLength() * 0.5f, yForDrive (drive) };
}

MultibandDisplay::Hit MultibandDisplay::hitTestAt (juce::Point<float> p) const
{
    if (! plot.contains (p)) return {};
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
    {
        if (nb > 1 && removeBounds (b).expanded (2.0f).contains (p) && pillBounds (b).getWidth() > 60.0f)
            return { Target::removeButton, b };
        if (driveHandle (b).getDistanceFrom (p) < 11.0f)
            return { Target::driveHandle, b };
    }

    for (int i = 0; i < nb - 1; ++i)
        if (std::abs (xForHz (crossover (i)) - p.x) < 6.0f && p.y > plot.getY() + 38.0f)
            return { Target::crossover, i };

    return { Target::bandBody, bandAtX (p.x) };
}

//==============================================================================
void MultibandDisplay::resized()
{
    auto r = getLocalBounds().toFloat();
    labelsRow = r.removeFromBottom (20.0f);
    plot = r;
}

void MultibandDisplay::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = juce::jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;

    if (analyzerMode != AnalyzerMode::off)
    {
        postSpectrum.update (dt);
        if (analyzerMode == AnalyzerMode::preAndPost) preSpectrum.update (dt);
    }

    for (int b = 0; b < kMaxBands; ++b)
    {
        const float lvl = proc.getEngine().getBand (b).outputLevelDb.load();
        const float target = juce::jlimit (0.0f, 1.0f, (lvl + 48.0f) / 48.0f);
        glow[(size_t) b] += (target - glow[(size_t) b]) * (target > glow[(size_t) b] ? 0.35f : 0.08f);

        const float want = b < numBands() ? 1.0f : 0.0f;
        bandAnim[(size_t) b] += (want - bandAnim[(size_t) b]) * 0.25f;
    }

    repaint();
}

//==============================================================================
void MultibandDisplay::paint (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    st::paint::well (g, plot, t.radiusLarge);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle (plot, t.radiusLarge);
    g.reduceClipRegion (clip);

    drawGrid (g);
    drawBands (g);
    drawSpectrum (g);
    drawCrossovers (g);

    g.restoreState();

    // Frequency labels
    g.setFont (st::fonts::medium (10.0f));
    g.setColour (t.textFaint);
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForHz (f);
        const auto txt = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f);
        g.drawText (txt, juce::Rectangle<float> (x - 20.0f, labelsRow.getY() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }
}

void MultibandDisplay::drawGrid (juce::Graphics& g)
{
    const auto& t = st::Theme::get();

    for (float decade : { 10.0f, 100.0f, 1000.0f, 10000.0f })
        for (int m = 1; m <= 9; ++m)
        {
            const float f = decade * (float) m;
            if (f < minHz || f > maxHz) continue;
            const float x = xForHz (f);
            const bool major = (m == 1 || m == 5 || m == 2);
            g.setColour (juce::Colours::white.withAlpha (major ? 0.045f : 0.022f));
            g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
        }

    // Drive scale (right edge)
    g.setFont (st::fonts::medium (9.5f));
    for (float db : { 12.0f, 24.0f, 36.0f, 48.0f })
    {
        const float y = yForDrive (db);
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
        g.setColour (t.textFaint.withAlpha (0.8f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getRight() - 30.0f, y - 12.0f, 24.0f, 11.0f),
                    juce::Justification::centredRight, false);
    }
}

void MultibandDisplay::drawSpectrum (juce::Graphics& g)
{
    if (analyzerMode == AnalyzerMode::off) return;
    const auto& t = st::Theme::get();
    auto area = plot.withTrimmedTop (36.0f);

    if (analyzerMode == AnalyzerMode::preAndPost)
    {
        auto pre = preSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
        g.setColour (t.textDim.withAlpha (0.35f));
        g.strokePath (pre, juce::PathStrokeType (1.0f));
    }

    auto fill = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, true);
    g.setGradientFill (juce::ColourGradient (t.text.withAlpha (0.10f), 0.0f, area.getY(),
                                             t.text.withAlpha (0.015f), 0.0f, area.getBottom(), false));
    g.fillPath (fill);

    auto line = postSpectrum.createPath (area, specMinDb, specMaxDb, minHz, maxHz, false);
    g.setColour (t.text.withAlpha (0.42f));
    g.strokePath (line, juce::PathStrokeType (1.2f));
}

void MultibandDisplay::drawBands (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int b = 0; b < nb; ++b)
    {
        const auto colour = t.seriesColour (b);
        const auto xr = bandXRange (b);
        const bool isSel = b == selected;
        const bool isHover = hover.index == b && hover.target != Target::crossover;
        const auto handle = driveHandle (b);

        const bool bypassed = proc.apvts.getRawParameterValue (ids::band (b, "bypass"))->load() > 0.5f;
        const bool muted = proc.apvts.getRawParameterValue (ids::band (b, "mute"))->load() > 0.5f;
        const float activity = bypassed || muted ? 0.35f : 1.0f;

        auto column = juce::Rectangle<float> (xr.getStart(), plot.getY(), xr.getLength(), plot.getHeight());

        // Column tint
        g.setColour (colour.withAlpha ((isSel ? 0.055f : 0.02f) + (isHover ? 0.02f : 0.0f)));
        g.fillRect (column);

        // Saturation fill below the drive line, glowing with the band's output level
        auto fillArea = column.withTop (handle.y).reduced (1.0f, 0.0f);
        const float glowAmt = 0.10f + 0.22f * glow[(size_t) b];
        juce::ColourGradient grad (colour.withAlpha (glowAmt * activity * (isSel ? 1.25f : 0.9f)), 0.0f, handle.y,
                                   colour.withAlpha (0.0f), 0.0f, plot.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRect (fillArea);

        // Drive line
        juce::Path driveLine;
        driveLine.startNewSubPath (xr.getStart() + 3.0f, handle.y);
        driveLine.lineTo (xr.getEnd() - 3.0f, handle.y);
        st::paint::glowStroke (g, driveLine, colour.withAlpha ((isSel ? 0.95f : 0.6f) * activity), isSel ? 2.0f : 1.5f, 6.0f);

        // Handle
        {
            const float rOuter = isSel || (hover.target == Target::driveHandle && hover.index == b) ? 8.0f : 6.5f;
            g.setColour (colour.withAlpha (0.18f));
            g.fillEllipse (juce::Rectangle<float> (rOuter * 3.0f, rOuter * 3.0f).withCentre (handle));
            g.setColour (t.bg1);
            g.fillEllipse (juce::Rectangle<float> (rOuter * 2.0f, rOuter * 2.0f).withCentre (handle));
            g.setColour (colour);
            g.drawEllipse (juce::Rectangle<float> (rOuter * 2.0f, rOuter * 2.0f).withCentre (handle), 2.0f);
            g.fillEllipse (juce::Rectangle<float> (rOuter * 0.8f, rOuter * 0.8f).withCentre (handle));
        }

        // Drive value while dragging / hovering the handle
        if ((drag.target == Target::driveHandle || drag.target == Target::bandBody) && drag.index == b)
        {
            auto txt = proc.bandParam (b, "drive").getCurrentValueAsText();
            g.setFont (st::fonts::semibold (11.5f));
            const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 16.0f;
            auto bubble = juce::Rectangle<float> (w, 20.0f).withCentre ({ handle.x, handle.y - 22.0f });
            g.setColour (t.bg3.withAlpha (0.95f));
            g.fillRoundedRectangle (bubble, 5.0f);
            g.setColour (colour.withAlpha (0.7f));
            g.drawRoundedRectangle (bubble, 5.0f, 1.0f);
            g.setColour (t.text);
            g.drawText (txt, bubble, juce::Justification::centred, false);
        }

        // Header pill with band number + style
        {
            auto pill = pillBounds (b);
            g.setColour (isSel ? colour.withAlpha (0.22f) : t.bg2.withAlpha (0.85f));
            g.fillRoundedRectangle (pill, 12.0f);
            g.setColour (colour.withAlpha (isSel ? 0.85f : 0.4f));
            g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);

            auto content = pill.reduced (9.0f, 0.0f);
            const bool canRemove = nb > 1 && pill.getWidth() > 60.0f;
            if (canRemove && (isHover || isSel))
            {
                const bool over = hover.target == Target::removeButton && hover.index == b;
                st::icons::draw (g, st::icons::Icon::close, removeBounds (b).reduced (2.5f), over ? t.text : t.textDim, 1.6f);
                content.removeFromRight (16.0f);
            }

            g.setColour (colour);
            g.fillEllipse (content.removeFromLeft (7.0f).withSizeKeepingCentre (7.0f, 7.0f));
            content.removeFromLeft (6.0f);

            if (pill.getWidth() > 70.0f)
            {
                const int style = proc.apvts.getRawParameterValue (ids::band (b, "style"))->load() > -1
                                    ? (int) proc.apvts.getRawParameterValue (ids::band (b, "style"))->load() : 0;
                g.setColour (isSel ? t.text : t.textDim);
                g.setFont (st::fonts::semibold (11.0f));
                g.drawText (getStyleInfo (style).name, content, juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour (isSel ? t.text : t.textDim);
                g.setFont (st::fonts::semibold (11.0f));
                g.drawText (juce::String (b + 1), content, juce::Justification::centredLeft, false);
            }
        }

        // State badges
        if (bypassed || muted)
        {
            g.setFont (st::fonts::caption (9.5f));
            g.setColour (t.textFaint);
            g.drawText (bypassed ? "BYPASSED" : "MUTED", column.withTop (plot.getBottom() - 22.0f), juce::Justification::centred, false);
        }
    }
}

void MultibandDisplay::drawCrossovers (juce::Graphics& g)
{
    const auto& t = st::Theme::get();
    const int nb = numBands();

    for (int i = 0; i < nb - 1; ++i)
    {
        const float x = xForHz (crossover (i));
        const bool active = (hover.target == Target::crossover && hover.index == i)
                         || (drag.target == Target::crossover && drag.index == i);

        g.setColour (active ? t.text.withAlpha (0.75f) : t.text.withAlpha (0.18f));
        g.fillRect (x - 0.5f, plot.getY() + 38.0f, 1.0f, plot.getHeight() - 38.0f);

        // grip
        auto grip = juce::Rectangle<float> (8.0f, 30.0f).withCentre ({ x, plot.getCentreY() + 20.0f });
        g.setColour (active ? t.text : t.bg4.brighter (0.25f));
        g.fillRoundedRectangle (grip, 4.0f);
        g.setColour (t.bg0.withAlpha (0.6f));
        for (int k = -1; k <= 1; ++k)
            g.fillRect (grip.getCentreX() - 2.0f, grip.getCentreY() + (float) k * 5.0f - 0.5f, 4.0f, 1.0f);

        if (active)
        {
            auto txt = st::params::formatHz (crossover (i));
            g.setFont (st::fonts::semibold (11.0f));
            const float w = st::fonts::stringWidth (g.getCurrentFont(), txt) + 14.0f;
            auto bubble = juce::Rectangle<float> (w, 20.0f).withCentre ({ x, plot.getBottom() - 16.0f });
            g.setColour (t.bg3);
            g.fillRoundedRectangle (bubble, 5.0f);
            g.setColour (t.text);
            g.drawText (txt, bubble, juce::Justification::centred, false);
        }
    }

    // Hint for an empty single-band setup
    if (nb == 1 && hover.target == Target::bandBody)
    {
        g.setFont (st::fonts::medium (11.5f));
        g.setColour (t.textFaint);
        g.drawText ("Double-click to split into bands", plot.withTrimmedBottom (8.0f).removeFromBottom (20.0f), juce::Justification::centred, false);
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

    if (drag.target == Target::none) return;

    if (drag.target == Target::removeButton)
        return;

    if (drag.target == Target::crossover)
    {
        proc.undoManager.beginNewTransaction ("Move crossover");
        dragParam = &proc.param (ids::crossover (drag.index));
        dragParam->beginChangeGesture();
        return;
    }

    // Band body or drive handle: select band, drag adjusts drive.
    if (selected != drag.index)
    {
        selected = drag.index;
        if (onBandSelected) onBandSelected (selected);
    }

    if (e.mods.isPopupMenu())
    {
        drag = {};
        return;
    }

    proc.undoManager.beginNewTransaction ("Drive");
    dragParam = &proc.bandParam (drag.index, "drive");
    dragStartValue = dragParam->convertFrom0to1 (dragParam->getValue());
    dragParam->beginChangeGesture();
    repaint();
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
        db = driveForY (e.position.y);
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
            selected = juce::jlimit (0, numBands() - 1, selected >= drag.index ? selected - 1 : selected);
            selected = juce::jmax (0, selected);
            if (onBandSelected) onBandSelected (selected);
        }
    }

    if (dragParam != nullptr)
        dragParam->endChangeGesture();

    dragParam = nullptr;
    drag = {};
    repaint();
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
        selected = juce::jmin (before + 1, numBands() - 1);
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
