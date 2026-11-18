#pragma once

// The individual saturation styles of ST Sat. Each style is a small struct with
//   prepare (Setup)   - design all filters from the actual (oversampled) rate, reset state
//   reset()           - clear state only (coefficients kept)
//   process (x, ctx)  - one sample of one channel
//   curve (x, ctx)    - static, stateless approximation of the transfer curve (UI)
// Levels are raw here; per-style unity trims and auto-gain live in SaturationCalibration.h.

#include "SaturationDetail.h"

namespace stsat::detail
{

struct Setup
{
    double fs = 48000.0;      // the rate processFrame() runs at (oversampled)
    double baseFs = 48000.0;  // host rate (fs / oversampling factor)
};

struct FrameCtx
{
    float G = 1.0f;   // drive as linear gain
    float dB = 0.0f;  // drive in dB (smoothed)
    float t = 0.0f;   // drive normalised 0..1 (0..48 dB)
    uint32_t version = 1;   // bumped whenever the drive changes (cheap coefficient-update check)
};

using SvfT = Svf::Type;

//==============================================================================
// TUBE

struct CleanTriode
{
    OnePole miller;
    double fs = 48000.0;
    uint32_t seen = 0;

    void prepare (const Setup& s) noexcept { fs = s.fs; seen = 0; reset(); }
    void reset() noexcept { miller.reset(); }

    static float core (float x, const FrameCtx& c) noexcept { return triode (0.55f * c.G * x); }

    float process (float x, const FrameCtx& c) noexcept
    {
        if (c.version != seen) { seen = c.version; miller.setCutoff (40000.0 - 18000.0 * c.t, fs); }
        return (float) miller.lp (core (x, c));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return core (x, c); }
};

struct WarmValve
{
    Svf preLow, postLow;
    OnePole couple, miller;
    double fs = 48000.0;
    uint32_t seen = 0;

    void prepare (const Setup& s) noexcept
    {
        fs = s.fs; seen = 0;
        preLow.set (SvfT::lowShelf, 220.0, 0.7, 3.5, fs);
        postLow.set (SvfT::lowShelf, 220.0, 0.7, -3.5, fs);
        couple.setCutoff (18.0, fs);
        reset();
    }

    void reset() noexcept { preLow.reset(); postLow.reset(); couple.reset(); miller.reset(); }

    static float stage2 (float y) noexcept { return -triode (-1.7f * y) * (1.0f / 1.7f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        if (c.version != seen) { seen = c.version; miller.setCutoff (24000.0 - 13000.0 * c.t, fs); }
        float y = triode (0.75f * c.G * preLow (x));
        y = stage2 ((float) couple.hp (y));
        return postLow ((float) miller.lp (y));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return stage2 (triode (0.75f * c.G * x)); }
};

struct PushedValve
{
    Env grid, supply;
    OnePole couple, miller;
    double fs = 48000.0;
    uint32_t seen = 0;

    void prepare (const Setup& s) noexcept
    {
        fs = s.fs; seen = 0;
        grid.set (5.0, 250.0, fs);
        supply.set (30.0, 350.0, fs);
        couple.setCutoff (30.0, fs);
        reset();
    }

    void reset() noexcept { grid.reset(); supply.reset(); couple.reset(); miller.reset(); }

    float process (float x, const FrameCtx& c) noexcept
    {
        if (c.version != seen) { seen = c.version; miller.setCutoff (20000.0 - 11000.0 * c.t, fs); }

        // grid current charges the coupling cap -> bias slides towards cutoff under load
        const float v = 1.3f * c.G * x - 0.7f * grid.value();
        grid.tick (std::max (0.0f, smax (v - 0.2f, 0.05f) - smax (-0.2f, 0.05f)));
        float y = (float) couple.hp (triode (v));
        y = -triode (-1.8f * y) * (1.0f / 1.8f);

        // power supply sag: the output ceiling drops with sustained load
        const float sag = supply.tick (y * y);
        const float ceil = 1.0f / (1.0f + 1.2f * std::sqrt (sag));
        y = ceil * algSat (y / ceil);
        return (float) miller.lp (y);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        float y = triode (1.3f * c.G * x);
        y = -triode (-1.8f * y) * (1.0f / 1.8f);
        return algSat (y);
    }
};

struct GlassRadio
{
    Svf hp, bell, lp;
    OnePole postLp, postHp;

    void prepare (const Setup& s) noexcept
    {
        hp.set (SvfT::highpass, 320.0, 0.65, 0.0, s.fs);
        bell.set (SvfT::bell, 1700.0, 1.0, 7.0, s.fs);
        lp.set (SvfT::lowpass, 4800.0, 0.85, 0.0, s.fs);
        postLp.setCutoff (6500.0, s.fs);
        postHp.setCutoff (180.0, s.fs);
        reset();
    }

    void reset() noexcept { hp.reset(); bell.reset(); lp.reset(); postLp.reset(); postHp.reset(); }

    static float core (float x, float G) noexcept { return asymAlg (triode (1.8f * G * x), 0.9f, 1.2f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float f = lp (bell (hp (x)));
        return (float) postHp.hp (postLp.lp (core (f, c.G)));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return core (x, c.G); }
};

struct PentodeEdge
{
    Svf preHi, preMid, postHi;
    Env screen;
    float off = 0.0f;
    static constexpr float bias = 0.08f;

    void prepare (const Setup& s) noexcept
    {
        preHi.set (SvfT::highShelf, 2400.0, 0.7, 6.0, s.fs);
        preMid.set (SvfT::bell, 1000.0, 0.8, 2.0, s.fs);
        postHi.set (SvfT::highShelf, 2400.0, 0.7, -4.5, s.fs);
        screen.set (1.5, 90.0, s.fs);
        off = kneeClip (bias, 0.75f, 0.95f, 0.22f);
        reset();
    }

    void reset() noexcept { preHi.reset(); preMid.reset(); postHi.reset(); screen.reset(); }

    float process (float x, const FrameCtx& c) noexcept
    {
        float v = 1.1f * c.G * preMid (preHi (x));
        v *= 1.0f / (1.0f + 0.3f * screen.value());   // screen-grid compression
        screen.tick (std::abs (v));
        return postHi (kneeClip (v + bias, 0.75f, 0.95f, 0.22f) - off);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        return kneeClip (1.1f * c.G * x + bias, 0.75f, 0.95f, 0.22f) - kneeClip (bias, 0.75f, 0.95f, 0.22f);
    }
};

//==============================================================================
// TAPE

struct SoftTape
{
    Svf pre, post, bump;
    OnePole hfLoss;
    Hysteresis hyst;
    Env level;
    BiasedAlg sat;

    void prepare (const Setup& s) noexcept
    {
        sat.set (0.04f);
        pre.set (SvfT::highShelf, 3500.0, 0.6, 5.0, s.fs);
        post.set (SvfT::highShelf, 3500.0, 0.6, -5.0, s.fs);
        bump.set (SvfT::bell, 65.0, 0.9, 1.2, s.fs);
        hfLoss.setCutoff (20000.0, s.fs);
        hyst.prepare (s.fs);
        level.set (5.0, 120.0, s.fs);
        reset();
    }

    void reset() noexcept { pre.reset(); post.reset(); bump.reset(); hfLoss.reset(); hyst.reset(); level.reset(); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 0.55f * c.G * pre (x);
        const float e = level.tick (std::abs (u));
        const float m = hyst.tick (u, 0.12f * e + 1.0e-6f);
        const float y = sat (0.85f * u + 0.15f * m);
        return (float) hfLoss.lp (bump (post (y)));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return algSat (0.55f * c.G * x); }
};

struct HotTape
{
    Svf pre, post, bump, dip;
    OnePole dyn;
    Hysteresis hyst;
    Env level, hfEnv;
    BiasedAlg sat;
    double piOverFs = 0.0, fcMax = 21000.0;

    void prepare (const Setup& s) noexcept
    {
        pre.set (SvfT::highShelf, 3000.0, 0.6, 8.0, s.fs);
        post.set (SvfT::highShelf, 3000.0, 0.6, -8.0, s.fs);
        bump.set (SvfT::bell, 55.0, 1.3, 2.5, s.fs);
        dip.set (SvfT::bell, 140.0, 1.0, -1.2, s.fs);
        hyst.prepare (s.fs);
        level.set (3.0, 100.0, s.fs);
        hfEnv.set (0.5, 60.0, s.fs);
        sat.set (0.1f);
        piOverFs = kPi / s.fs;
        fcMax = std::min (21000.0, 0.45 * s.fs);
        reset();
    }

    void reset() noexcept
    {
        pre.reset(); post.reset(); bump.reset(); dip.reset(); dyn.reset();
        hyst.reset(); level.reset(); hfEnv.reset();
    }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 1.1f * c.G * pre (x);
        const float e = level.tick (std::abs (u));
        const float m = hyst.tick (u, 0.2f * e + 1.0e-6f);
        float y = post (sat (0.7f * u + 0.3f * m));

        // level-dependent HF loss (self-erasure / HF compression)
        const float he = hfEnv.tick (std::abs (y));
        dyn.setG (fastTan (piOverFs * fcMax / (1.0 + 1.5 * he)));
        y = (float) dyn.lp (y);
        return dip (bump (y));
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        BiasedAlg s; s.set (0.1f);
        return s (1.1f * c.G * x);
    }
};

struct WornCassette
{
    OnePole hp, dyn;
    Svf pre, post, lp2, box;
    Hysteresis hyst;
    Env level, hfEnv;
    BiasedAlg sat;
    double piOverFs = 0.0, fcMax = 12000.0;

    void prepare (const Setup& s) noexcept
    {
        hp.setCutoff (45.0, s.fs);
        pre.set (SvfT::highShelf, 2200.0, 0.6, 9.0, s.fs);
        post.set (SvfT::highShelf, 2200.0, 0.6, -9.0, s.fs);
        lp2.set (SvfT::lowpass, 9500.0, 0.65, 0.0, s.fs);
        box.set (SvfT::bell, 420.0, 0.8, 1.5, s.fs);
        hyst.prepare (s.fs);
        level.set (3.0, 150.0, s.fs);
        hfEnv.set (0.5, 80.0, s.fs);
        sat.set (0.12f);
        piOverFs = kPi / s.fs;
        fcMax = std::min (12000.0, 0.45 * s.fs);
        reset();
    }

    void reset() noexcept
    {
        hp.reset(); dyn.reset(); pre.reset(); post.reset(); lp2.reset(); box.reset();
        hyst.reset(); level.reset(); hfEnv.reset();
    }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 1.4f * c.G * pre ((float) hp.hp (x));
        const float e = level.tick (std::abs (u));
        float z = 0.65f * u + 0.35f * hyst.tick (u, 0.25f * e + 1.0e-6f);

        // worn-oxide grit: small-signal crossover graininess relative to programme level
        const float w = 0.05f * e + 1.0e-6f;
        const float r = z / w;
        z *= 1.0f - 0.3f / (1.0f + r * r);

        float y = post (sat (z));
        const float he = hfEnv.tick (std::abs (y));
        dyn.setG (fastTan (piOverFs * fcMax / (1.0 + 3.0 * he)));
        y = (float) dyn.lp (y);
        return box (lp2 (y));
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        BiasedAlg s; s.set (0.12f);
        return s (1.4f * c.G * x);
    }
};

//==============================================================================
// SATURATION

struct GentleGlow
{
    float off = 0.0f, norm = 1.0f;
    static constexpr float bias = 0.15f;

    void prepare (const Setup&) noexcept { off = rootSat (bias); norm = 1.0f / rootSatSlope (bias); }
    void reset() noexcept {}

    float process (float x, const FrameCtx& c) const noexcept { return asymAlg ((rootSat (0.5f * c.G * x + bias) - off) * norm, 0.25f, 0.25f); }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        return asymAlg ((rootSat (0.5f * c.G * x + bias) - rootSat (bias)) / rootSatSlope (bias), 0.25f, 0.25f);
    }
};

struct Dense
{
    Svf preLow, postLow;
    Env rms;
    OnePole couple, smooth;

    void prepare (const Setup& s) noexcept
    {
        preLow.set (SvfT::lowShelf, 300.0, 0.7, 2.5, s.fs);
        postLow.set (SvfT::lowShelf, 300.0, 0.7, -2.5, s.fs);
        rms.set (15.0, 160.0, s.fs);
        couple.setCutoff (12.0, s.fs);
        smooth.setCutoff (15000.0, s.fs);
        reset();
    }

    void reset() noexcept { preLow.reset(); postLow.reset(); rms.reset(); couple.reset(); smooth.reset(); }

    static float s1 (float u) noexcept { return algSat (1.4f * u) * (1.0f / 1.4f); }
    static float s2 (float u) noexcept { return asymAlg (1.5f * u, 0.9f, 1.15f) * (1.0f / 1.5f); }
    static float s3 (float u) noexcept { return fastTanh (1.6f * u) * (1.0f / 1.6f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        float u = 0.7f * c.G * preLow (x);
        const float g = 1.0f / (1.0f + 0.5f * std::sqrt (rms.value()));   // slow "glue" compression
        rms.tick (u * u);
        u *= g;
        float y = (float) couple.hp (s1 (u));
        y = (float) smooth.lp (s2 (y));
        return postLow (s3 (y));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return s3 (s2 (s1 (0.7f * c.G * x))); }
};

struct IronTransformer
{
    OnePole split;
    Env level;
    Hysteresis hyst;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (100.0, s.fs);
        level.set (10.0, 200.0, s.fs);
        hyst.prepare (s.fs);
        reset();
    }

    void reset() noexcept { split.reset(); level.reset(); hyst.reset(); }

    static float core (float flux) noexcept { return kneeClip (flux, 0.65f, 0.62f, 0.3f); }
    static float out (float y) noexcept { return asymAlg (y, 0.45f, 0.47f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 1.2f * c.G * x;
        const float lo = (float) split.lp (u);
        const float hi = u - lo;
        const float e = level.tick (std::abs (lo));
        const float flux = 0.8f * lo + 0.2f * hyst.tick (lo, 0.06f * e + 1.0e-6f);
        return out (core (flux) + hi);
    }

    static float curve (float x, const FrameCtx& c) noexcept { return out (core (1.2f * c.G * x)); }
};

struct ConsoleBus
{
    OnePole split;
    BiasedAlg classA;
    float prev = 0.0f, slewStep = 1.0f;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (70.0, s.fs);
        classA.set (0.07f);
        slewStep = (float) (2.0 * kPi * 20000.0 / s.fs);   // full-scale 20 kHz sine just reaches the slew limit
        reset();
    }

    void reset() noexcept { split.reset(); prev = 0.0f; }

    static float rails (float y) noexcept { return kneeClip (y, 1.1f, 1.0f, 0.12f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 0.9f * c.G * x;
        const float lo = (float) split.lp (u);
        const float a = classA (algSat (1.2f * lo) * (1.0f / 1.2f) + (u - lo));
        prev += slewStep * fastTanh ((a - prev) / slewStep);   // soft slew-rate limiting (rate independent)
        return rails (prev);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        BiasedAlg a; a.set (0.07f);
        return rails (a (0.9f * c.G * x));
    }
};

//==============================================================================
// AMP  (lows below the split bypass the preamp gain chain -> tight low end)

struct CleanCombo
{
    OnePole split, couple;
    Svf bright, mid, post;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (110.0, s.fs);
        couple.setCutoff (60.0, s.fs);
        bright.set (SvfT::highShelf, 2500.0, 0.7, 4.0, s.fs);
        mid.set (SvfT::bell, 450.0, 0.8, -3.0, s.fs);
        post.set (SvfT::lowpass, 11000.0, 0.6, 0.0, s.fs);
        reset();
    }

    void reset() noexcept { split.reset(); couple.reset(); bright.reset(); mid.reset(); post.reset(); }

    static float pre (float h) noexcept { return triode (1.5f * h) * (1.0f / 1.5f); }
    static float power (float h) noexcept { return pushPull (1.3f * h) * (1.0f / 1.3f); }
    static float low (float l) noexcept { return 0.5f * algSat (l * 2.0f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 0.6f * c.G * x;
        const float lo = (float) split.lp (u);
        float h = pre (bright (u - lo));
        h = power (mid ((float) couple.hp (h)));
        return post (h) + low (lo);
    }

    static float curve (float x, const FrameCtx& c) noexcept { return power (pre (0.6f * c.G * x)); }
};

struct CrunchStack
{
    OnePole split, cpl1, mil1, cpl2, mil2;
    Svf preMid, toneMid, toneHi, post;
    Env sag;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (160.0, s.fs);
        cpl1.setCutoff (90.0, s.fs);  mil1.setCutoff (12000.0, s.fs);
        cpl2.setCutoff (45.0, s.fs);  mil2.setCutoff (9000.0, s.fs);
        preMid.set (SvfT::bell, 750.0, 0.9, 5.0, s.fs);
        toneMid.set (SvfT::bell, 480.0, 0.7, -4.0, s.fs);
        toneHi.set (SvfT::highShelf, 2800.0, 0.7, 3.0, s.fs);
        post.set (SvfT::lowpass, 7500.0, 0.7, 0.0, s.fs);
        sag.set (20.0, 250.0, s.fs);
        reset();
    }

    void reset() noexcept
    {
        split.reset(); cpl1.reset(); mil1.reset(); cpl2.reset(); mil2.reset();
        preMid.reset(); toneMid.reset(); toneHi.reset(); post.reset(); sag.reset();
    }

    static float low (float l) noexcept { return 0.45f * algSat (l * (2.5f / 0.45f)); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = c.G * x;
        const float lo = (float) split.lp (u);
        float h = triode (2.5f * preMid (u - lo));
        h = (float) mil1.lp (cpl1.hp (h));
        h = -triode (-2.2f * h);
        h = (float) mil2.lp (cpl2.hp (h));
        h = toneHi (toneMid (h));
        const float ceil = 1.0f / (1.0f + 0.6f * std::sqrt (sag.value()));
        sag.tick (h * h);
        h = ceil * pushPull (1.4f * h / ceil) * (1.0f / 1.4f);
        return 0.6f * post (h) + low (lo);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        float h = triode (2.5f * c.G * x);
        h = -triode (-2.2f * h);
        return 0.6f * pushPull (1.4f * h) * (1.0f / 1.4f);
    }
};

struct LeadStack
{
    OnePole split, cpl1, mil1, cpl2, mil2, cpl3, mil3;
    Svf preMid, toneMid, presence, post;
    Env sag;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (180.0, s.fs);
        cpl1.setCutoff (120.0, s.fs); mil1.setCutoff (11000.0, s.fs);
        cpl2.setCutoff (60.0, s.fs);  mil2.setCutoff (8000.0, s.fs);
        cpl3.setCutoff (40.0, s.fs);  mil3.setCutoff (7000.0, s.fs);
        preMid.set (SvfT::bell, 850.0, 1.0, 4.0, s.fs);
        toneMid.set (SvfT::bell, 520.0, 0.8, -3.0, s.fs);
        presence.set (SvfT::bell, 1500.0, 0.9, 2.0, s.fs);
        post.set (SvfT::lowpass, 6500.0, 0.7, 0.0, s.fs);
        sag.set (15.0, 300.0, s.fs);
        reset();
    }

    void reset() noexcept
    {
        split.reset(); cpl1.reset(); mil1.reset(); cpl2.reset(); mil2.reset(); cpl3.reset(); mil3.reset();
        preMid.reset(); toneMid.reset(); presence.reset(); post.reset(); sag.reset();
    }

    static float low (float l) noexcept { return 0.35f * algSat (l * (4.0f / 0.35f)); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = c.G * x;
        const float lo = (float) split.lp (u);
        float h = triode (3.0f * preMid (u - lo));
        h = (float) mil1.lp (cpl1.hp (h));
        h = -triode (-3.2f * h);
        h = (float) mil2.lp (cpl2.hp (h));
        h = triode (2.5f * h);
        h = (float) mil3.lp (cpl3.hp (h));
        h = toneMid (h);
        const float ceil = 1.0f / (1.0f + 1.0f * std::sqrt (sag.value()));
        sag.tick (h * h);
        h = ceil * pushPull (1.5f * h / ceil) * (1.0f / 1.5f);
        return 0.55f * post (presence (h)) + low (lo);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        float h = triode (3.0f * c.G * x);
        h = -triode (-3.2f * h);
        h = triode (2.5f * h);
        return 0.55f * pushPull (1.5f * h) * (1.0f / 1.5f);
    }
};

struct BassRig
{
    OnePole split;
    Svf growl, post, scoop;
    Env comp;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (250.0, s.fs);
        growl.set (SvfT::bell, 900.0, 1.0, 3.0, s.fs);
        post.set (SvfT::lowpass, 4200.0, 0.7, 0.0, s.fs);
        scoop.set (SvfT::bell, 450.0, 0.7, -2.5, s.fs);
        comp.set (10.0, 150.0, s.fs);
        reset();
    }

    void reset() noexcept { split.reset(); growl.reset(); post.reset(); scoop.reset(); comp.reset(); }

    static float diode (float v) noexcept
    {
        return v >= 0.0f ? 0.35f * std::asinh (v * (1.0f / 0.35f)) : 0.55f * std::asinh (v * (1.0f / 0.55f));
    }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = c.G * x;
        const float lo = (float) split.lp (u);
        const float gc = 1.0f / (1.0f + 0.4f * std::sqrt (comp.value()));
        comp.tick (lo * lo);
        const float l = triode (1.4f * lo * gc) * (1.0f / 1.4f);
        const float h = post (diode (5.0f * growl (u - lo)));
        return scoop (l + 0.4f * h);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        const float u = c.G * x;
        return triode (1.4f * u) * (1.0f / 1.4f) * 0.5f + 0.4f * 0.5f * diode (5.0f * u);
    }
};

struct FuzzPedal
{
    OnePole split, couple;
    Svf post, tone;
    Env env;

    void prepare (const Setup& s) noexcept
    {
        split.setCutoff (100.0, s.fs);
        couple.setCutoff (40.0, s.fs);
        post.set (SvfT::lowpass, 5500.0, 0.7, 0.0, s.fs);
        tone.set (SvfT::highShelf, 2500.0, 0.7, -3.0, s.fs);
        env.set (2.0, 120.0, s.fs);
        reset();
    }

    void reset() noexcept { split.reset(); couple.reset(); post.reset(); tone.reset(); env.reset(); }

    static float q1 (float v) noexcept { return kneeClip (bjt (v), 0.8f, 5.0f, 0.05f); }
    static float q2 (float a) noexcept { return kneeClip (3.0f * a, 0.7f, 0.75f, 0.03f); }
    static float low (float l) noexcept { return 0.3f * algSat (l * (2.0f / 0.3f)); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = c.G * x;
        const float lo = (float) split.lp (u);
        const float hi = u - lo;
        // starved bias: quiet passages sit off-centre (gated, spitty), loud ones pull it back
        const float e = env.value();
        const float bias = 0.15f * (0.1f / (e + 0.1f));
        env.tick (std::abs (hi));
        const float a = (float) couple.hp (q1 (10.0f * hi + bias) - q1 (bias));
        return 0.7f * tone (post (q2 (a))) + low (lo);
    }

    static float curve (float x, const FrameCtx& c) noexcept { return 0.7f * q2 (q1 (10.0f * c.G * x)); }
};

//==============================================================================
// CLIP

struct SoftClip
{
    void prepare (const Setup&) noexcept {}
    void reset() noexcept {}
    static float curve (float x, const FrameCtx& c) noexcept { return kneeClip (1.2f * c.G * x, 0.65f, 0.65f, 0.45f); }
    float process (float x, const FrameCtx& c) const noexcept { return curve (x, c); }
};

struct HardClip
{
    void prepare (const Setup&) noexcept {}
    void reset() noexcept {}
    static float curve (float x, const FrameCtx& c) noexcept { return kneeClip (c.G * x, 1.0f, 1.0f, 0.015f); }
    float process (float x, const FrameCtx& c) const noexcept { return curve (x, c); }
};

struct DiodePair
{
    OnePole rc;

    void prepare (const Setup& s) noexcept { rc.setCutoff (18000.0, s.fs); reset(); }
    void reset() noexcept { rc.reset(); }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        const float u = c.G * x;
        return u >= 0.0f ? 0.22f * std::asinh (u * (1.0f / 0.22f)) : 0.4f * std::asinh (u * (1.0f / 0.4f));
    }

    float process (float x, const FrameCtx& c) noexcept { return (float) rc.lp (curve (x, c)); }
};

struct Silicon
{
    OnePole couple, bw;

    void prepare (const Setup& s) noexcept { couple.setCutoff (20.0, s.fs); bw.setCutoff (20000.0, s.fs); reset(); }
    void reset() noexcept { couple.reset(); bw.reset(); }

    static float st1 (float v) noexcept { return kneeClip (bjt (v), 0.85f, 4.0f, 0.08f); }
    static float st2 (float a) noexcept { return -kneeClip (bjt (-1.1f * a), 0.9f, 4.0f, 0.1f) * (1.0f / 1.1f); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float a = (float) couple.hp (st1 (1.2f * c.G * x));
        return (float) bw.lp (st2 (a));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return st2 (st1 (1.2f * c.G * x)); }
};

//==============================================================================
// FX

struct FullWave
{
    OnePole dc;

    void prepare (const Setup& s) noexcept { dc.setCutoff (12.0, s.fs); reset(); }
    void reset() noexcept { dc.reset(); }

    static float core (float x, float G) noexcept
    {
        const float u = G * x;
        return asymAlg (rootSat (std::sqrt (u * u + 1.0e-6f) - 1.0e-3f), 0.2f, 0.2f);
    }

    float process (float x, const FrameCtx& c) noexcept { return (float) dc.hp (core (x, c.G)); }
    static float curve (float x, const FrameCtx& c) noexcept { return core (x, c.G); }
};

struct HalfWave
{
    OnePole dc;

    void prepare (const Setup& s) noexcept { dc.setCutoff (12.0, s.fs); reset(); }
    void reset() noexcept { dc.reset(); }

    static float core (float x, float G) noexcept
    {
        return asymAlg (smax (G * x, 1.0e-3f) - smax (0.0f, 1.0e-3f), 0.4f, 0.4f);
    }

    float process (float x, const FrameCtx& c) noexcept { return (float) dc.hp (core (x, c.G)); }
    static float curve (float x, const FrameCtx& c) noexcept { return core (x, c.G); }
};

struct Wavefold
{
    OnePole dc;

    void prepare (const Setup& s) noexcept { dc.setCutoff (12.0, s.fs); reset(); }
    void reset() noexcept { dc.reset(); }

    static float core (float x, const FrameCtx& c) noexcept
    {
        constexpr float halfPi = 1.57079632679f;
        const float b = 0.15f * c.t;                       // asymmetric fold grows with drive
        const float u = 1.1f * std::pow (c.G, 0.7f) * x;   // gentler drive taper (folds multiply fast)
        return (fastSin (halfPi * (u + b)) - fastSin (halfPi * b)) * (1.0f / halfPi);
    }

    float gTaper = 1.0f; uint32_t seen = 0;

    float process (float x, const FrameCtx& c) noexcept
    {
        if (c.version != seen) { seen = c.version; gTaper = 1.1f * std::pow (c.G, 0.7f); }
        constexpr float halfPi = 1.57079632679f;
        const float b = 0.15f * c.t;
        const float y = (fastSin (halfPi * (gTaper * x + b)) - fastSin (halfPi * b)) * (1.0f / halfPi);
        return (float) dc.hp (y);
    }

    static float curve (float x, const FrameCtx& c) noexcept { return core (x, c); }
};

struct Sputter
{
    Env env;
    OnePole dc;

    void prepare (const Setup& s) noexcept { env.set (1.0, 300.0, s.fs); dc.setCutoff (12.0, s.fs); reset(); }
    void reset() noexcept { env.reset(); dc.reset(); }

    static float shape (float u, float e, float t) noexcept
    {
        // envelope-tracking dead zone: as a note decays faster than the held envelope,
        // it drops into the gap and sputters / gates; asymmetric (starved) thresholds.
        const float dz = (0.06f + 0.22f * t) * e;
        const float k = 0.15f * dz + 1.0e-6f;
        const float d = smax (u - dz, k) - smax (-u - 1.4f * dz, k) - (smax (-dz, k) - smax (-1.4f * dz, k));
        return kneeClip (2.0f * d, 0.6f, 0.75f, 0.08f);
    }

    float process (float x, const FrameCtx& c) noexcept
    {
        // envelope of the pre-drive signal, so drive moves never leave the gate stuck shut
        const float e = env.tick (std::abs (1.5f * x)) * c.G;
        return (float) dc.hp (shape (1.5f * c.G * x, e, c.t));
    }

    static float curve (float x, const FrameCtx& c) noexcept { return shape (1.5f * c.G * x, 1.5f * c.G * 0.7f, c.t); }
};

struct Smear
{
    Env env;
    OnePole slow, smudge, dc;
    double piOverFs = 0.0, fcMax = 9000.0;

    void prepare (const Setup& s) noexcept
    {
        env.set (4.0, 120.0, s.fs);
        slow.setCutoff (35.0, s.fs);
        dc.setCutoff (12.0, s.fs);
        piOverFs = kPi / s.fs;
        fcMax = std::min (9000.0, 0.45 * s.fs);
        reset();
    }

    void reset() noexcept { env.reset(); slow.reset(); smudge.reset(); dc.reset(); }

    float process (float x, const FrameCtx& c) noexcept
    {
        const float u = 1.5f * c.G * x;
        const float e = env.tick (std::abs (u));
        // bias follows the envelope and the low-frequency body of the signal -> dynamic intermod smear
        const float b = 0.35f * e + 0.5f * (float) slow.lp (u);
        const float y = algSat (u + b) - algSat (b);
        smudge.setG (fastTan (piOverFs * fcMax / (1.0 + 4.0 * e)));
        const float s = (float) smudge.lp (y);
        return (float) dc.hp (0.55f * y + 0.45f * s);
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        const float u = 1.5f * c.G * x;
        const float b = 0.35f * std::abs (u) * 0.5f;
        return algSat (u + b) - algSat (b);
    }
};

struct RateCrush
{
    double fs = 48000.0, baseFs = 48000.0;
    double phase = 1.0, inc = 1.0;
    float held = 0.0f, levels = 2048.0f, invLevels = 1.0f / 2048.0f, inGain = 1.0f;
    uint32_t seen = 0;

    void prepare (const Setup& s) noexcept { fs = s.fs; baseFs = s.baseFs; seen = 0; reset(); }
    void setBaseRate (double b) noexcept { baseFs = b; seen = 0; }
    void reset() noexcept { phase = 1.0; held = 0.0f; }

    void update (const FrameCtx& c) noexcept
    {
        seen = c.version;
        const double ratio = 1.5 * std::exp2 (4.0 * c.t);                 // hold 1.5x .. 24x the base period
        inc = std::min (1.0, baseFs / (ratio * fs));
        levels = (float) std::exp2 (11.0 - 7.5 * c.t);                      // ~12 bit .. ~4.5 bit
        invLevels = 1.0f / levels;
        inGain = 1.0f + 2.0f * c.t;
    }

    static float quant (float v, float levels, float invLevels) noexcept
    {
        return std::floor (v * levels + 0.5f) * invLevels;
    }

    float process (float x, const FrameCtx& c) noexcept
    {
        if (c.version != seen) update (c);
        phase += inc;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            held = quant (algSat (inGain * x), levels, invLevels);
        }
        return held;
    }

    static float curve (float x, const FrameCtx& c) noexcept
    {
        const float levels = std::exp2 (11.0f - 7.5f * c.t);
        return quant (algSat ((1.0f + 2.0f * c.t) * x), levels, 1.0f / levels);
    }
};

//==============================================================================

constexpr int kNumStylesInternal = 27;

/** All style instances for one channel; dispatch by index. */
struct StyleBank
{
    CleanTriode s0; WarmValve s1; PushedValve s2; GlassRadio s3; PentodeEdge s4;
    SoftTape s5; HotTape s6; WornCassette s7;
    GentleGlow s8; Dense s9; IronTransformer s10; ConsoleBus s11;
    CleanCombo s12; CrunchStack s13; LeadStack s14; BassRig s15; FuzzPedal s16;
    SoftClip s17; HardClip s18; DiodePair s19; Silicon s20;
    FullWave s21; HalfWave s22; Wavefold s23; Sputter s24; Smear s25; RateCrush s26;

    template <typename Fn>
    void forAll (Fn&& fn)
    {
        fn (s0); fn (s1); fn (s2); fn (s3); fn (s4); fn (s5); fn (s6); fn (s7); fn (s8); fn (s9);
        fn (s10); fn (s11); fn (s12); fn (s13); fn (s14); fn (s15); fn (s16); fn (s17); fn (s18);
        fn (s19); fn (s20); fn (s21); fn (s22); fn (s23); fn (s24); fn (s25); fn (s26);
    }

    template <typename Fn>
    void with (int i, Fn&& fn)
    {
        switch (i)
        {
            case 0:  fn (s0);  break;  case 1:  fn (s1);  break;  case 2:  fn (s2);  break;
            case 3:  fn (s3);  break;  case 4:  fn (s4);  break;  case 5:  fn (s5);  break;
            case 6:  fn (s6);  break;  case 7:  fn (s7);  break;  case 8:  fn (s8);  break;
            case 9:  fn (s9);  break;  case 10: fn (s10); break;  case 11: fn (s11); break;
            case 12: fn (s12); break;  case 13: fn (s13); break;  case 14: fn (s14); break;
            case 15: fn (s15); break;  case 16: fn (s16); break;  case 17: fn (s17); break;
            case 18: fn (s18); break;  case 19: fn (s19); break;  case 20: fn (s20); break;
            case 21: fn (s21); break;  case 22: fn (s22); break;  case 23: fn (s23); break;
            case 24: fn (s24); break;  case 25: fn (s25); break;  default: fn (s26); break;
        }
    }

    void prepare (const Setup& s) noexcept { forAll ([&] (auto& st) { st.prepare (s); }); }
    void reset (int i) noexcept { with (i, [] (auto& st) { st.reset(); }); }
    void resetAll() noexcept { forAll ([] (auto& st) { st.reset(); }); }

    float process (int i, float x, const FrameCtx& c) noexcept
    {
        switch (i)
        {
            case 0:  return s0.process (x, c);   case 1:  return s1.process (x, c);
            case 2:  return s2.process (x, c);   case 3:  return s3.process (x, c);
            case 4:  return s4.process (x, c);   case 5:  return s5.process (x, c);
            case 6:  return s6.process (x, c);   case 7:  return s7.process (x, c);
            case 8:  return s8.process (x, c);   case 9:  return s9.process (x, c);
            case 10: return s10.process (x, c);  case 11: return s11.process (x, c);
            case 12: return s12.process (x, c);  case 13: return s13.process (x, c);
            case 14: return s14.process (x, c);  case 15: return s15.process (x, c);
            case 16: return s16.process (x, c);  case 17: return s17.process (x, c);
            case 18: return s18.process (x, c);  case 19: return s19.process (x, c);
            case 20: return s20.process (x, c);  case 21: return s21.process (x, c);
            case 22: return s22.process (x, c);  case 23: return s23.process (x, c);
            case 24: return s24.process (x, c);  case 25: return s25.process (x, c);
            default: return s26.process (x, c);
        }
    }

    static float curve (int i, float x, const FrameCtx& c) noexcept
    {
        switch (i)
        {
            case 0:  return CleanTriode::curve (x, c);      case 1:  return WarmValve::curve (x, c);
            case 2:  return PushedValve::curve (x, c);      case 3:  return GlassRadio::curve (x, c);
            case 4:  return PentodeEdge::curve (x, c);      case 5:  return SoftTape::curve (x, c);
            case 6:  return HotTape::curve (x, c);          case 7:  return WornCassette::curve (x, c);
            case 8:  return GentleGlow::curve (x, c);       case 9:  return Dense::curve (x, c);
            case 10: return IronTransformer::curve (x, c);  case 11: return ConsoleBus::curve (x, c);
            case 12: return CleanCombo::curve (x, c);       case 13: return CrunchStack::curve (x, c);
            case 14: return LeadStack::curve (x, c);        case 15: return BassRig::curve (x, c);
            case 16: return FuzzPedal::curve (x, c);        case 17: return SoftClip::curve (x, c);
            case 18: return HardClip::curve (x, c);         case 19: return DiodePair::curve (x, c);
            case 20: return Silicon::curve (x, c);          case 21: return FullWave::curve (x, c);
            case 22: return HalfWave::curve (x, c);         case 23: return Wavefold::curve (x, c);
            case 24: return Sputter::curve (x, c);          case 25: return Smear::curve (x, c);
            default: return RateCrush::curve (x, c);
        }
    }
};

} // namespace stsat::detail
