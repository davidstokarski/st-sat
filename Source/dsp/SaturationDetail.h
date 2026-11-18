#pragma once

// Small, self-contained DSP building blocks for the ST Sat saturation engine.
// Everything here is header-only, allocation-free and designed from the actual
// sample rate so it behaves identically from 44.1 kHz up to 768 kHz.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace stsat::detail
{

constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Memoryless shaping primitives (all odd/C2 at the origin unless stated)

/** Smooth max(z, 0) with knee width k (hyperbolic). */
inline float smax (float z, float k) noexcept { return 0.5f * (z + std::sqrt (z * z + k * k)); }

/** Algebraic soft saturator, slope 1 at 0, ceiling +-1. Gentler knee than tanh. */
inline float algSat (float x) noexcept { return x / std::sqrt (1.0f + x * x); }

/** Rational tanh approximation (exact +-1 and zero slope at |x| = 3). */
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.0f, 3.0f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/** Asymmetric algebraic saturator: slope 1 at 0, ceilings +1/kp and -1/kn (C2 at 0). */
inline float asymAlg (float x, float kp, float kn) noexcept
{
    const float kx = (x >= 0.0f ? kp : kn) * x;
    return x / std::sqrt (1.0f + kx * kx);
}

/** "Root" saturator: slope 1 at 0, grows like sqrt|x| -> never flattens, very gentle. */
inline float rootSat (float x) noexcept { return x / std::sqrt (std::sqrt (1.0f + x * x)); }
inline float rootSatSlope (float x) noexcept
{
    const float a = 1.0f + x * x;
    return (1.0f + 0.5f * x * x) / (a * std::sqrt (std::sqrt (a)));
}

/** Smooth-knee clipper: linear around 0, approaches +tPos / -tNeg with knee width k. f(0) = 0. */
inline float kneeClip (float x, float tPos, float tNeg, float k) noexcept
{
    const float off = smax (-tPos, k) - smax (-tNeg, k);
    return x - smax (x - tPos, k) + smax (-x - tNeg, k) + off;
}

/** sin() approximation, accurate to ~2e-4 for any argument (range reduced). */
inline float fastSin (float x) noexcept
{
    constexpr float twoPi = 6.28318530718f, invTwoPi = 0.159154943092f, halfPi = 1.57079632679f, pi = 3.14159265359f;
    x -= twoPi * std::floor (x * invTwoPi + 0.5f);
    if (x > halfPi)       x = pi - x;
    else if (x < -halfPi) x = -pi - x;
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.166666667f + x2 * (0.00833333310f + x2 * -0.000198408740f)));
}

/** tan() approximation for 0 <= x <= ~1.4 (Pade 3/2), used for per-sample cutoff modulation. */
inline double fastTan (double x) noexcept
{
    x = std::min (x, 1.4);
    const double x2 = x * x;
    return x * (15.0 - x2) / (15.0 - 6.0 * x2);
}

//==============================================================================
// Triode model: Child-Langmuir 3/2 law with smooth cutoff, grid-conduction
// compression of positive swings and plate-current limiting (load line).
// Normalised so that triode(0) = 0 and slope at idle = 1. Asymmetric ceilings
// (~ +1.85 / -0.8): mostly 2nd harmonic at low level, grows richer when pushed.

inline float triodeRaw (float v) noexcept
{
    const float vg = v - 0.85f * smax (v - 0.5f, 0.35f);
    const float s  = smax (1.0f + vg, 0.25f);
    const float i  = s * std::sqrt (s);
    const float r  = i * (1.0f / 3.0f);
    return i / std::sqrt (1.0f + r * r);
}

struct TriodeConsts { float idle, invSlope; };

inline TriodeConsts makeTriodeConsts() noexcept
{
    const float h = 1.0e-3f;
    const float idle = triodeRaw (0.0f);
    const float slope = (triodeRaw (h) - triodeRaw (-h)) / (2.0f * h);
    return { idle, 1.0f / slope };
}

inline const TriodeConsts kTriode = makeTriodeConsts();

inline float triodeExact (float v) noexcept { return (triodeRaw (v) - kTriode.idle) * kTriode.invSlope; }

/** The triode curve is used in many cascaded stages, so it is tabulated once (8k points over
    the useful grid range) and read back with Catmull-Rom interpolation: error < 1e-6, ~3x cheaper.
    Outside [-4, 60] the exact curve is flat to within 1e-3 (cutoff / plate limit). */
struct TriodeTable
{
    static constexpr int size = 8192;
    static constexpr float lo = -4.0f, hi = 60.0f;
    static constexpr float step = (hi - lo) / (float) size, invStep = (float) size / (hi - lo);
    float v[size + 4] {};

    TriodeTable() noexcept
    {
        for (int i = 0; i < size + 4; ++i)
            v[i] = triodeExact (lo + (float) (i - 1) * step);   // v[1] corresponds to x = lo
    }

    float operator() (float x) const noexcept
    {
        const float pos = std::clamp ((x - lo) * invStep, 0.0f, (float) size - 1.0e-3f);
        const int i = (int) pos;
        const float t = pos - (float) i;
        const float* p = v + i;   // p[0..3] = points i-1 .. i+2
        const float a = p[0], b = p[1], c = p[2], d = p[3];
        return b + 0.5f * t * (c - a + t * (2.0f * a - 5.0f * b + 4.0f * c - d + t * (3.0f * (b - c) + d - a)));
    }
};

inline const TriodeTable kTriodeTable;

inline float triode (float v) noexcept { return kTriodeTable (v); }

/** Push-pull pair of triodes: even orders cancel, symmetric ceiling ~1.3. */
inline float pushPull (float v) noexcept { return 0.5f * (triode (v) - triode (-v)); }

/** Bipolar transistor (common emitter, normalised): exponential expansion upwards, soft cutoff to -1. */
inline float bjt (float v) noexcept { return std::exp (std::clamp (v, -14.0f, 3.0f)) - 1.0f; }

/** Biased algebraic stage with static offset removed and unity slope: even + odd harmonics. */
struct BiasedAlg
{
    float bias = 0.0f, offset = 0.0f, norm = 1.0f;

    void set (float b) noexcept
    {
        bias = b;
        offset = algSat (b);
        norm = std::pow (1.0f + b * b, 1.5f);
    }

    float operator() (float x) const noexcept { return (algSat (x + bias) - offset) * norm; }
};

//==============================================================================
// Linear building blocks (double state: robust at 768 kHz with very low cutoffs)

/** Topology-preserving one-pole (LP; HP = x - LP). */
struct OnePole
{
    double G = 0.0, s = 0.0;

    void setCutoff (double fc, double fs) noexcept
    {
        const double g = std::tan (kPi * std::clamp (fc, 1.0e-3, 0.49 * fs) / fs);
        G = g / (1.0 + g);
    }

    void setG (double g) noexcept { G = g / (1.0 + g); }

    void reset() noexcept { s = 0.0; }

    double lp (double x) noexcept
    {
        const double v = (x - s) * G;
        const double y = v + s;
        s = y + v;
        return y;
    }

    double hp (double x) noexcept { return x - lp (x); }
};

/** Andrew Simper's linear trapezoidal state-variable filter (stable under any setting). */
struct Svf
{
    enum class Type { lowpass, highpass, bandpass, bell, lowShelf, highShelf };

    double a1 = 0.0, a2 = 0.0, a3 = 0.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
    double ic1 = 0.0, ic2 = 0.0;

    void set (Type type, double fc, double q, double gainDb, double fs) noexcept
    {
        fc = std::clamp (fc, 1.0, 0.49 * fs);
        const double w = std::tan (kPi * fc / fs);
        const double A = std::pow (10.0, gainDb / 40.0);
        double g = w, k = 1.0 / q;

        switch (type)
        {
            case Type::lowpass:   m0 = 0.0; m1 = 0.0; m2 = 1.0; break;
            case Type::highpass:  m0 = 1.0; m1 = -k;  m2 = -1.0; break;
            case Type::bandpass:  m0 = 0.0; m1 = k;   m2 = 0.0; break;
            case Type::bell:      k = 1.0 / (q * A); m0 = 1.0; m1 = k * (A * A - 1.0); m2 = 0.0; break;
            case Type::lowShelf:  g = w / std::sqrt (A); m0 = 1.0; m1 = k * (A - 1.0); m2 = A * A - 1.0; break;
            case Type::highShelf: g = w * std::sqrt (A); m0 = A * A; m1 = k * (1.0 - A) * A; m2 = 1.0 - A * A; break;
        }

        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }

    double tick (double v0) noexcept
    {
        const double v3 = v0 - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }

    float operator() (float x) noexcept { return (float) tick ((double) x); }
};

/** Attack/release envelope follower (one-pole, sample-rate aware, denormal-safe). */
struct Env
{
    double att = 1.0, rel = 1.0, s = 0.0;

    void set (double attackMs, double releaseMs, double fs) noexcept
    {
        att = 1.0 - std::exp (-1.0 / (std::max (attackMs, 0.001) * 1.0e-3 * fs));
        rel = 1.0 - std::exp (-1.0 / (std::max (releaseMs, 0.001) * 1.0e-3 * fs));
    }

    void reset() noexcept { s = 0.0; }

    float tick (float x) noexcept
    {
        s += ((double) x - s) * ((double) x > s ? att : rel);
        if (s < 1.0e-25) s = 0.0;
        return (float) s;
    }

    float value() const noexcept { return (float) s; }
};

/** Rate-independent magnetic memory: a play (backlash) operator, lightly smoothed. */
struct Hysteresis
{
    double m = 0.0, leak = 0.0;
    OnePole smooth;

    void prepare (double fs) noexcept
    {
        smooth.setCutoff (15000.0, fs);
        leak = 1.0 - std::exp (-1.0 / (0.02 * fs));   // remanence relaxes over ~20 ms (no stuck offsets)
    }

    void reset() noexcept { m = 0.0; smooth.reset(); }

    float tick (float x, float width) noexcept
    {
        const double xd = x, w = width;
        m -= m * leak;
        if (xd > m + w)      m = xd - w;
        else if (xd < m - w) m = xd + w;
        if (std::abs (m) < 1.0e-25) m = 0.0;
        return (float) smooth.lp (m);
    }
};

} // namespace stsat::detail
