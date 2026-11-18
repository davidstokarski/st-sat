// ST Sat - offline test / calibration tool for the saturation engine.
//
//   StSatDspTests --test            run all assertions (exit code != 0 on failure)
//   StSatDspTests --calibrate [f]   measure unity trims + auto-gain tables, print them (and write f)
//   StSatDspTests --render <dir>    write 24-bit WAVs of every style at several drives
//   StSatDspTests --harmonics       H1..H7 of a 1 kHz sine per style and drive
//   StSatDspTests --spectrum        octave-band tonal balance vs input (pink noise) per style
//   StSatDspTests --bench           CPU cost per style relative to a plain tanh()

#include "dsp/Saturation.h"
#include "dsp/SaturationDetail.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <vector>

using namespace stsat;
using Buffer = std::vector<float>;

namespace
{

//==============================================================================
// Signals

struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 1) {}
    float white() noexcept
    {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return (float) ((double) (s >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0);
    }
};

double rms (const float* x, size_t n)
{
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += (double) x[i] * x[i];
    return std::sqrt (acc / (double) std::max<size_t> (1, n));
}

double toDb (double v) { return 20.0 * std::log10 (juce::jmax (v, 1.0e-12)); }
double rmsDb (const Buffer& b, size_t skip = 0) { return toDb (rms (b.data() + skip, b.size() - skip)); }

void scaleToRmsDb (Buffer& b, double targetDb)
{
    const double g = std::pow (10.0, targetDb / 20.0) / juce::jmax (rms (b.data(), b.size()), 1.0e-12);
    for (auto& v : b) v = (float) (v * g);
}

/** Pink-ish noise (Kellet), band-limited 20 Hz..20 kHz, normalised to the given RMS. */
Buffer pinkNoise (double fs, size_t n, uint64_t seed, double rmsDbTarget = -18.0)
{
    Rng rng (seed);
    Buffer out (n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    detail::Svf hp, lp;
    hp.set (detail::Svf::Type::highpass, 20.0, 0.7071, 0.0, fs);
    lp.set (detail::Svf::Type::lowpass, juce::jmin (20000.0, 0.45 * fs), 0.7071, 0.0, fs);

    for (size_t i = 0; i < n; ++i)
    {
        const double w = rng.white();
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520; b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        out[i] = (float) lp.tick (hp.tick (p));
    }
    scaleToRmsDb (out, rmsDbTarget);
    return out;
}

Buffer sine (double fs, size_t n, double hz, double amp)
{
    Buffer b (n);
    for (size_t i = 0; i < n; ++i)
        b[i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / fs));
    return b;
}

/** Test programme for listening: log sweep, short gap, then a synthetic drum + bass + chord loop. */
Buffer listeningSignal (double fs)
{
    const double twoPi = juce::MathConstants<double>::twoPi;
    Buffer out;

    // log sweep 30 Hz -> 16 kHz, 3 s, -12 dBFS peak
    {
        const double T = 3.0, f0 = 30.0, f1 = 16000.0, k = std::log (f1 / f0);
        const size_t n = (size_t) (T * fs);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            const double ph = twoPi * f0 * T / k * (std::exp (t / T * k) - 1.0);
            const double fade = juce::jmin (1.0, t / 0.02, (T - t) / 0.02);
            out.push_back ((float) (0.25 * fade * std::sin (ph)));
        }
        out.resize (out.size() + (size_t) (0.3 * fs), 0.0f);
    }

    // 2 bars at 120 BPM (4 s)
    {
        const size_t start = out.size();
        const double bar = 2.0, beat = 0.5;
        const size_t n = (size_t) (2 * bar * fs);
        out.resize (start + n, 0.0f);
        Rng rng (7);
        detail::OnePole hatHp, snareHp, bassLp;
        hatHp.setCutoff (7000.0, fs); snareHp.setCutoff (1200.0, fs); bassLp.setCutoff (900.0, fs);
        const double bassNotes[] = { 55.0, 55.0, 65.41, 73.42, 55.0, 82.41, 73.42, 65.41 };
        const double chord[] = { 220.0, 261.63, 329.63 };
        double bassPh = 0.0;

        for (size_t i = 0; i < n; ++i)
        {
            const double t = (double) i / fs;
            const double tb = std::fmod (t, 2.0 * beat);      // time since kick
            const double ts = std::fmod (t + beat, 2.0 * beat); // time since snare
            const double th = std::fmod (t, beat * 0.5);      // eighth hats
            double s = 0.0;

            // kick: pitch-swept sine
            const double kf = 48.0 + 110.0 * std::exp (-tb * 40.0);
            s += 0.7 * std::exp (-tb * 7.0) * std::sin (twoPi * (48.0 * tb + 110.0 / 40.0 * (1.0 - std::exp (-tb * 40.0))));
            juce::ignoreUnused (kf);

            // snare: tone + noise
            const double nz = rng.white();
            s += 0.25 * std::exp (-ts * 25.0) * std::sin (twoPi * 190.0 * ts) + 0.35 * std::exp (-ts * 18.0) * snareHp.hp (nz);

            // hats
            s += 0.12 * std::exp (-th * 70.0) * hatHp.hp (rng.white());

            // bass: saw-ish through a lowpass, eighth notes
            const double bf = bassNotes[(size_t) (t / beat) % 8];
            bassPh += bf / fs; bassPh -= std::floor (bassPh);
            const double tn = std::fmod (t, beat);
            s += 0.3 * std::exp (-tn * 3.0) * bassLp.lp (2.0 * bassPh - 1.0);

            // chord stab on the off-beat
            const double tc = std::fmod (t + 0.25, beat);
            for (double f : chord)
                s += 0.05 * std::exp (-tc * 5.0) * std::sin (twoPi * f * t);

            out[start + i] = (float) (0.55 * s);
        }
    }
    return out;
}

//==============================================================================
// Measurement helpers

/** BS.1770 style K-weighting (shelf + high-pass), returns RMS in dB. */
double kWeightedDb (const Buffer& x, double fs, size_t skip)
{
    detail::Svf shelf, hp;
    shelf.set (detail::Svf::Type::highShelf, 1681.0, 0.7071, 4.0, fs);
    hp.set (detail::Svf::Type::highpass, 38.0, 0.5, 0.0, fs);
    double acc = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = hp.tick (shelf.tick (x[i]));
        if (i >= skip) acc += v * v;
    }
    return toDb (std::sqrt (acc / (double) (x.size() - skip)));
}

double meanAbsDb (const Buffer& x, size_t skip)
{
    double acc = 0.0;
    for (size_t i = skip; i < x.size(); ++i) acc += x[i];
    return toDb (std::abs (acc / (double) (x.size() - skip)));
}

float maxJump (const Buffer& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < juce::jmin (to, x.size()); ++i)
        m = juce::jmax (m, std::abs (x[i] - x[i - 1]));
    return m;
}

bool allFinite (const Buffer& x)
{
    for (auto v : x) if (! std::isfinite (v)) return false;
    return true;
}

float peakAbs (const Buffer& x, size_t from = 0)
{
    float m = 0.0f;
    for (size_t i = from; i < x.size(); ++i) m = juce::jmax (m, std::abs (x[i]));
    return m;
}

//==============================================================================
// Runners

struct RunOpts
{
    double fs = 96000.0;
    int os = 2;              // hint only (direct runs)
    float driveDb = 0.0f;
    bool autoGain = false;
    bool bypass = false;
};

void configure (Saturator& s, int style, const RunOpts& o, int channels)
{
    s.setLatencyAlignedOversampling (o.os);
    s.setStyle (style);
    s.setDrive (o.driveDb);
    s.setAutoGain (o.autoGain);
    s.setCalibrationBypass (o.bypass);
    s.prepare (o.fs, 512, channels);
    s.reset();
}

/** Direct run at o.fs (stereo, identical channels), returns channel 0. */
Buffer runDirect (int style, const RunOpts& o, const Buffer& in, std::function<void (Saturator&, size_t)> perSample = {})
{
    Saturator sat;
    configure (sat, style, o, 2);
    Buffer out (in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (perSample) perSample (sat, i);
        float fr[2] = { in[i], in[i] };
        sat.processFrame (fr, 2);
        out[i] = fr[0];
    }
    return out;
}

/** Plugin-like round trip: input at base rate -> up x factor -> saturate -> down. */
Buffer runRoundTrip (int style, double baseFs, int factor, float driveDb, bool autoGain, bool bypass, const Buffer& in)
{
    const size_t order = (size_t) std::round (std::log2 ((double) factor));
    juce::dsp::Oversampling<float> ovs (1, order, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    constexpr size_t block = 512;
    ovs.initProcessing (block);

    Saturator sat;
    RunOpts o; o.fs = baseFs * factor; o.os = factor; o.driveDb = driveDb; o.autoGain = autoGain; o.bypass = bypass;
    configure (sat, style, o, 1);

    Buffer out (in.size());
    Buffer tmp (block);
    for (size_t pos = 0; pos < in.size(); pos += block)
    {
        const size_t n = juce::jmin (block, in.size() - pos);
        std::copy (in.begin() + (long) pos, in.begin() + (long) (pos + n), tmp.begin());
        float* chans[] = { tmp.data() };
        juce::dsp::AudioBlock<float> blk (chans, 1, n);
        auto up = ovs.processSamplesUp (blk);
        float* upCh[] = { up.getChannelPointer (0) };
        sat.processBlock (upCh, 1, (int) up.getNumSamples());
        ovs.processSamplesDown (blk);
        std::copy (tmp.begin(), tmp.begin() + (long) n, out.begin() + (long) pos);
    }
    return out;
}

//==============================================================================
// Reporting

struct Checker
{
    int failures = 0, checks = 0;

    void expect (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::printf ("  FAIL: %s\n", what.toRawUTF8());
        }
    }
};

const char* nm (int s) { return getStyleInfo (s).name; }

//==============================================================================
// --calibrate

constexpr double kCalBaseFs = 48000.0;
constexpr int kCalFactor = 2;
constexpr double kCalSeconds = 3.0;

int runCalibrate (const juce::String& writePath)
{
    const int N = getNumStyles();
    const auto in = pinkNoise (kCalBaseFs, (size_t) (kCalSeconds * kCalBaseFs), 1234);
    const size_t skip = (size_t) (0.5 * kCalBaseFs);
    const double inRms = rmsDb (in, skip);

    std::vector<float> trims ((size_t) N);
    std::vector<std::array<float, 9>> comp ((size_t) N);

    for (int s = 0; s < N; ++s)
    {
        double k0 = 0.0;
        for (int d = 0; d < 9; ++d)
        {
            const auto out = runRoundTrip (s, kCalBaseFs, kCalFactor, 6.0f * (float) d, false, true, in);
            const double k = kWeightedDb (out, kCalBaseFs, skip);
            if (d == 0)
            {
                trims[(size_t) s] = (float) (inRms - rmsDb (out, skip));
                k0 = k;
            }
            comp[(size_t) s][(size_t) d] = (float) (k0 - k);
        }
        std::fprintf (stderr, "  calibrated %2d %-18s trim %+6.2f dB  comp@48 %+6.2f dB\n", s, nm (s), trims[(size_t) s], comp[(size_t) s][8]);
    }

    juce::String txt;
    txt << "#pragma once\n\n"
        << "// GENERATED by `StSatDspTests --calibrate` - do not edit by hand.\n"
        << "// Unity trims: plain RMS of band-limited pink noise at -18 dBFS RMS, drive 0 dB.\n"
        << "// Auto gain: K-weighted loudness relative to drive 0, at drive 0, 6, ... 48 dB.\n"
        << "// Measured as a plugin would run: 48 kHz -> 2x oversampled -> saturate -> 48 kHz.\n\n"
        << "namespace stsat::calib\n{\n\n"
        << "constexpr int kNumStyles = " << N << ";\n"
        << "constexpr int kNumDrivePoints = 9;\n"
        << "constexpr float kDriveStepDb = 6.0f;\n\n"
        << "constexpr float kUnityTrimDb[kNumStyles] = {\n";
    for (int s = 0; s < N; ++s)
        txt << "    " << juce::String (trims[(size_t) s], 3) << "f,   // " << nm (s) << "\n";
    txt << "};\n\nconstexpr float kAutoGainDb[kNumStyles][kNumDrivePoints] = {\n";
    for (int s = 0; s < N; ++s)
    {
        txt << "    { ";
        for (int d = 0; d < 9; ++d)
            txt << juce::String (comp[(size_t) s][(size_t) d], 3) << "f" << (d < 8 ? ", " : "");
        txt << " },   // " << nm (s) << "\n";
    }
    txt << "};\n\n} // namespace stsat::calib\n";

    std::printf ("%s", txt.toRawUTF8());

    if (writePath.isNotEmpty())
    {
        juce::File f (writePath);
        if (f.replaceWithText (txt))
            std::fprintf (stderr, "wrote %s\n", f.getFullPathName().toRawUTF8());
        else
            std::fprintf (stderr, "could not write %s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}

//==============================================================================
// --test

int runTests()
{
    Checker chk;
    const int N = getNumStyles();
    const auto t0 = std::chrono::steady_clock::now();

    // ---------------------------------------------------------------- catalogue
    std::printf ("[catalogue]\n");
    chk.expect (N == getStyleNames().size(), "style names count");
    chk.expect (getStyleCategories().size() == 6, "six categories");
    std::printf ("  %d styles, categories: %s\n", N, getStyleCategories().joinIntoString (", ").toRawUTF8());

    // ---------------------------------------------------------------- levels
    std::printf ("\n[levels: pink noise -18 dBFS RMS, 48k x2 round trip]\n");
    std::printf ("  %-18s %8s %8s | auto-gain K-loudness deviation vs drive 0 (dB) | %8s %8s\n", "style", "unity", "DCmax", "maxDev", "@44k1");
    {
        const auto in = pinkNoise (kCalBaseFs, (size_t) (2.0 * kCalBaseFs), 98765);   // different seed than calibration
        const auto in44 = pinkNoise (44100.0, (size_t) (2.0 * 44100.0), 555);
        const size_t skip = (size_t) (0.4 * kCalBaseFs), skip44 = (size_t) (0.4 * 44100.0);
        const double inRms = rmsDb (in, skip), inRms44 = rmsDb (in44, skip44);
        double worstUnity = 0.0, worstAuto = 0.0, worstAuto44 = 0.0;

        for (int s = 0; s < N; ++s)
        {
            double k0 = 0.0, maxDev = 0.0, dcMax = -400.0, unity = 0.0;
            juce::String devs;
            bool finite = true;

            for (int d = 0; d <= 16; ++d)   // 0, 3, ... 48 dB
            {
                const float drive = 3.0f * (float) d;
                const auto out = runRoundTrip (s, kCalBaseFs, kCalFactor, drive, true, false, in);
                finite = finite && allFinite (out);
                dcMax = juce::jmax (dcMax, meanAbsDb (out, skip));
                const double k = kWeightedDb (out, kCalBaseFs, skip);
                if (d == 0) { k0 = k; unity = rmsDb (out, skip) - inRms; }
                maxDev = juce::jmax (maxDev, std::abs (k - k0));
                if (d % 4 == 0) devs << juce::String (k - k0, 1).paddedLeft (' ', 5);
            }

            // same tables at a different host rate / oversampling factor (1x @ 44.1k)
            double k0b = 0.0, maxDev44 = 0.0;
            for (int d = 0; d <= 8; ++d)
            {
                const auto out = runRoundTrip (s, 44100.0, 1, 6.0f * (float) d, true, false, in44);
                const double k = kWeightedDb (out, 44100.0, skip44);
                if (d == 0)
                {
                    k0b = k;
                    const double u44 = rmsDb (out, skip44) - inRms44;
                    chk.expect (std::abs (u44) <= 2.0, juce::String (nm (s)) + " unity @44.1k 1x: " + juce::String (u44, 2) + " dB");
                }
                maxDev44 = juce::jmax (maxDev44, std::abs (k - k0b));
            }

            std::printf ("  %-18s %+7.2f %+7.1f |%s | %7.2f %8.2f\n", nm (s), unity, dcMax, devs.toRawUTF8(), maxDev, maxDev44);
            worstUnity = juce::jmax (worstUnity, std::abs (unity));
            worstAuto = juce::jmax (worstAuto, maxDev);
            worstAuto44 = juce::jmax (worstAuto44, maxDev44);

            chk.expect (finite, juce::String (nm (s)) + " output finite");
            chk.expect (dcMax < -60.0, juce::String (nm (s)) + " DC " + juce::String (dcMax, 1) + " dBFS");
            chk.expect (std::abs (unity) <= 1.5, juce::String (nm (s)) + " unity at drive 0: " + juce::String (unity, 2) + " dB");
            chk.expect (maxDev <= 1.5, juce::String (nm (s)) + " auto-gain flatness: " + juce::String (maxDev, 2) + " dB");
            // no oversampling at all: aliasing shifts loudness of the folding/rectifying styles a little
            chk.expect (maxDev44 <= 3.5, juce::String (nm (s)) + " auto-gain flatness @44.1k 1x: " + juce::String (maxDev44, 2) + " dB");
        }
        std::printf ("  worst |unity| %.2f dB, worst auto-gain deviation %.2f dB (96k), %.2f dB (44.1k 1x)\n", worstUnity, worstAuto, worstAuto44);
    }

    // ---------------------------------------------------------------- style switching
    std::printf ("\n[style switching: 100 Hz sine, drive 12 dB, 96 kHz - max sample jump]\n");
    {
        RunOpts o; o.driveDb = 12.0f; o.autoGain = true;
        const size_t n = (size_t) (0.6 * o.fs), sw = (size_t) (0.3 * o.fs);
        const auto in = sine (o.fs, n, 100.0, 0.25);
        std::vector<float> steady ((size_t) N);
        for (int s = 0; s < N; ++s)
            steady[(size_t) s] = maxJump (runDirect (s, o, in), sw, n);

        float worstRatio = 0.0f;
        int pairs = 0;
        for (int a = 0; a < N; ++a)
        {
            for (int step : { 1, 5, 13 })
            {
                const int b = (a + step) % N;
                const auto out = runDirect (a, o, in, [&] (Saturator& s, size_t i) { if (i == sw) s.setStyle (b); });
                const float j = maxJump (out, sw - 10, sw + (size_t) (0.08 * o.fs));
                const float ref = juce::jmax (steady[(size_t) a], steady[(size_t) b]);
                const float ratio = j / ref;
                worstRatio = juce::jmax (worstRatio, ratio);
                ++pairs;
                chk.expect (allFinite (out) && j <= 1.5f * ref + 0.002f,
                            juce::String (nm (a)) + " -> " + nm (b) + ": jump " + juce::String (j, 4) + " vs steady " + juce::String (ref, 4));
            }
        }
        // rapid re-switching (queued while a fade is running)
        const auto out = runDirect (0, o, in, [&] (Saturator& s, size_t i) {
            if (i >= sw && (i - sw) % 300 == 0) s.setStyle ((int) ((i - sw) / 300) % N);
        });
        float ref = 0.0f;
        for (auto v : steady) ref = juce::jmax (ref, v);
        const float jr = maxJump (out, sw - 10, n);
        chk.expect (allFinite (out) && jr <= 1.5f * ref + 0.002f, "rapid switching jump " + juce::String (jr, 4));
        std::printf ("  %d transitions, worst jump / steady-state jump = %.2f; rapid re-switching jump %.4f (max steady %.4f)\n",
                     pairs, worstRatio, jr, ref);
    }

    // ---------------------------------------------------------------- drive / auto gain automation
    std::printf ("\n[drive steps 0 <-> 48 dB and auto-gain toggling]\n");
    {
        RunOpts o; o.driveDb = 0.0f; o.autoGain = true;
        const size_t n = (size_t) (0.8 * o.fs);
        const auto in = sine (o.fs, n, 100.0, 0.25);
        float worst = 0.0f;
        for (int s = 0; s < N; ++s)
        {
            // reference: steady-state jumps of every configuration the automation visits
            RunOpts o48 = o; o48.driveDb = 48.0f;
            RunOpts o48off = o48; o48off.autoGain = false;
            const float ref = juce::jmax (maxJump (runDirect (s, o, in), n / 4, n),
                                          maxJump (runDirect (s, o48, in), n / 4, n),
                                          maxJump (runDirect (s, o48off, in), n / 4, n));
            const auto out = runDirect (s, o, in, [&] (Saturator& sat, size_t i) {
                if (i == n / 4) sat.setDrive (48.0f);
                if (i == n / 2) sat.setAutoGain (false);
                if (i == 5 * n / 8) sat.setAutoGain (true);
                if (i == 3 * n / 4) sat.setDrive (0.0f);
            });
            const float j = maxJump (out, n / 4 - 10, n);
            worst = juce::jmax (worst, j / ref);
            chk.expect (allFinite (out) && j <= 1.5f * ref + 0.01f,
                        juce::String (nm (s)) + " drive/auto-gain automation jump " + juce::String (j, 4) + " vs " + juce::String (ref, 4));
        }
        std::printf ("  worst automation jump / steady jump = %.2f\n", worst);
    }

    // ---------------------------------------------------------------- stability
    for (auto [baseFs, factor] : { std::pair { 44100.0, 1 }, std::pair { 96000.0, 8 } })
    {
        const double fs = baseFs * factor;
        std::printf ("\n[stability @ %.1f kHz (%dx), drive 48 dB, hot noise + full-scale bass + impulses + silence]\n", fs / 1000.0, factor);
        RunOpts o; o.fs = fs; o.os = factor; o.driveDb = 48.0f; o.autoGain = false;
        const size_t nSig = (size_t) (0.4 * fs), nSil = (size_t) (0.8 * fs);
        auto in = pinkNoise (fs, nSig, 42, -6.0);
        const auto bass = sine (fs, nSig, 50.0, 1.0);
        for (size_t i = 0; i < nSig; ++i)
            in[i] += bass[i] * (i > nSig / 2 ? 1.0f : 0.0f) + ((i % (size_t) (0.05 * fs)) == 0 ? 2.0f : 0.0f);
        in.resize (nSig + nSil, 0.0f);

        float worstPeak = 0.0f, worstTail = 0.0f;
        for (int s = 0; s < N; ++s)
        {
            const auto out = runDirect (s, o, in, [&] (Saturator& sat, size_t i) {
                if (i < nSig / 4) sat.setDrive (48.0f * (float) i / (float) (nSig / 4));   // drive sweep while playing
            });
            const float pk = peakAbs (out);
            const float tail = (float) rms (out.data() + out.size() - (size_t) (0.1 * fs), (size_t) (0.1 * fs));
            worstPeak = juce::jmax (worstPeak, pk);
            worstTail = juce::jmax (worstTail, tail);
            chk.expect (allFinite (out), juce::String (nm (s)) + " finite");
            chk.expect (pk < 32.0f, juce::String (nm (s)) + " bounded (peak " + juce::String (pk, 2) + ")");
            chk.expect (toDb (tail) < -100.0, juce::String (nm (s)) + " decays to silence (" + juce::String (toDb (tail), 1) + " dBFS)");
        }
        std::printf ("  worst peak %.2f (%.1f dBFS), worst tail %.1f dBFS\n", worstPeak, toDb (worstPeak), toDb (worstTail));
    }

    // ---------------------------------------------------------------- silence & stereo
    std::printf ("\n[silence in -> silence out, channel independence]\n");
    {
        RunOpts o; o.driveDb = 48.0f;
        const Buffer zeros ((size_t) (0.1 * o.fs), 0.0f);
        float worst = 0.0f;
        for (int s = 0; s < N; ++s)
        {
            const float pk = peakAbs (runDirect (s, o, zeros));
            worst = juce::jmax (worst, pk);
            chk.expect (pk < 1.0e-6f, juce::String (nm (s)) + " silence -> silence (peak " + juce::String (pk, 8) + ")");
        }

        // L and R must be processed independently
        Saturator sat;
        configure (sat, 6, o, 2);
        float maxR = 0.0f;
        const auto in = pinkNoise (o.fs, 20000, 9);
        for (auto v : in) { float fr[2] = { v, 0.0f }; sat.processFrame (fr, 2); maxR = juce::jmax (maxR, std::abs (fr[1])); }
        chk.expect (maxR < 1.0e-6f, "no crosstalk between channels");
        std::printf ("  silence peak %.2g, crosstalk peak %.2g\n", (double) worst, (double) maxR);
    }

    // ---------------------------------------------------------------- transfer curve
    {
        Saturator sat;
        bool ok = true;
        for (int s = 0; s < N; ++s)
        {
            sat.setStyle (s);
            for (float d : { 0.0f, 24.0f, 48.0f })
            {
                sat.setDrive (d);
                for (int i = 0; i <= 64; ++i)
                    ok = ok && std::isfinite (sat.getTransferCurveSample (-1.0f + (float) i / 32.0f));
            }
        }
        chk.expect (ok, "transfer curves finite");
    }

    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("\n%d checks, %d failures (%.1f s)\n", chk.checks, chk.failures, secs);
    std::printf (chk.failures == 0 ? "ALL TESTS PASSED\n" : "TESTS FAILED\n");
    return chk.failures == 0 ? 0 : 1;
}

//==============================================================================
// --harmonics

int runHarmonics()
{
    const double fs = 96000.0, f0 = 1000.0;
    const size_t settle = 24000, len = 96000;   // exactly 1000 cycles
    const auto in = sine (fs, settle + len, f0, 0.25);
    std::printf ("1 kHz sine, -12 dBFS peak, 96 kHz (2x hint), auto gain on.\n"
                 "H1 in dBFS; H2..H7 in dB relative to the INPUT fundamental (so rectifiers read sensibly); THD+ = sum(H2..H7) / input\n");
    std::printf ("%-18s %5s %7s %7s %7s %7s %7s %7s %7s %7s\n", "style", "drive", "H1", "H2", "H3", "H4", "H5", "H6", "H7", "THD%");

    for (int s = 0; s < getNumStyles(); ++s)
    {
        for (float d : { 0.0f, 12.0f, 24.0f, 36.0f })
        {
            RunOpts o; o.fs = fs; o.driveDb = d; o.autoGain = true;
            const auto out = runDirect (s, o, in);
            double h[8] = {};
            for (int k = 1; k <= 7; ++k)
            {
                double re = 0.0, im = 0.0;
                const double w = juce::MathConstants<double>::twoPi * f0 * k / fs;
                for (size_t i = 0; i < len; ++i)
                {
                    re += out[settle + i] * std::cos (w * (double) i);
                    im += out[settle + i] * std::sin (w * (double) i);
                }
                h[k] = 2.0 * std::sqrt (re * re + im * im) / (double) len;
            }
            double thd = 0.0;
            for (int k = 2; k <= 7; ++k) thd += h[k] * h[k];
            std::printf ("%-18s %5.0f %7.1f", nm (s), d, toDb (h[1]));
            for (int k = 2; k <= 7; ++k) std::printf (" %7.1f", toDb (h[k] / 0.25));
            std::printf (" %7.2f\n", 100.0 * std::sqrt (thd) / 0.25);
        }
    }
    return 0;
}

//==============================================================================
// --spectrum : octave-band level change (out - in) on pink noise

int runSpectrum()
{
    const double fs = kCalBaseFs;
    const auto in = pinkNoise (fs, (size_t) (3.0 * fs), 77);
    const size_t skip = (size_t) (0.5 * fs);
    const double bands[] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

    auto bandLevels = [&] (const Buffer& x) {
        std::vector<double> lv;
        for (double fc : bands)
        {
            detail::Svf bp1, bp2;
            bp1.set (detail::Svf::Type::bandpass, fc, 1.4, 0.0, fs);
            bp2.set (detail::Svf::Type::bandpass, fc, 1.4, 0.0, fs);
            double acc = 0.0;
            for (size_t i = 0; i < x.size(); ++i) { const double v = bp2.tick (bp1.tick (x[i])); if (i >= skip) acc += v * v; }
            lv.push_back (toDb (std::sqrt (acc / (double) (x.size() - skip))));
        }
        return lv;
    };

    const auto ref = bandLevels (in);
    std::printf ("octave-band level change vs input (dB), pink noise -18 dBFS RMS, 48k x2, auto gain on\n%-18s %5s", "style", "drive");
    for (double b : bands) std::printf (" %6.0f", b);
    std::printf ("\n");
    for (int s = 0; s < getNumStyles(); ++s)
        for (float d : { 0.0f, 24.0f })
        {
            const auto lv = bandLevels (runRoundTrip (s, fs, 2, d, true, false, in));
            std::printf ("%-18s %5.0f", nm (s), d);
            for (size_t i = 0; i < lv.size(); ++i) std::printf (" %+6.1f", lv[i] - ref[i]);
            std::printf ("\n");
        }
    return 0;
}

//==============================================================================
// --bench

int runBench()
{
    const double fs = 96000.0;
    const auto in = pinkNoise (fs, (size_t) fs, 3);
    constexpr int reps = 7;

    // baseline: stereo per-frame tanh with gain (the "textbook" saturator)
    double baseNs = 1e30;
    {
        volatile float sink = 0.0f;
        for (int r = 0; r < reps; ++r)
        {
            const auto t0 = std::chrono::steady_clock::now();
            float acc = 0.0f;
            for (auto v : in)
            {
                float fr[2] = { v * 4.0f, v * 3.9f };
                for (auto& x : fr) x = std::tanh (x);
                acc += fr[0] + fr[1];
            }
            sink = acc;
            baseNs = juce::jmin (baseNs, std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / (double) in.size());
        }
        juce::ignoreUnused (sink);
    }
    std::printf ("CPU per stereo frame @96 kHz, drive 12 dB (best of %d). baseline std::tanh: %.1f ns\n", reps, baseNs);

    for (int s = 0; s < getNumStyles(); ++s)
    {
        Saturator sat;
        RunOpts o; o.fs = fs; o.driveDb = 12.0f; o.autoGain = true;
        configure (sat, s, o, 2);
        double best = 1e30;
        for (int r = 0; r < reps; ++r)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (auto v : in) { float fr[2] = { v, v * 0.97f }; sat.processFrame (fr, 2); }
            best = juce::jmin (best, std::chrono::duration<double, std::nano> (std::chrono::steady_clock::now() - t0).count() / (double) in.size());
        }
        std::printf ("  %2d %-18s %6.1f ns  x%.1f\n", s, nm (s), best, best / baseNs);
    }
    return 0;
}

//==============================================================================
// --render

int runRender (const juce::String& dirPath)
{
    juce::File dir (dirPath);
    if (! dir.createDirectory())
    {
        std::printf ("cannot create %s\n", dir.getFullPathName().toRawUTF8());
        return 1;
    }

    const double baseFs = 48000.0;
    const auto in = listeningSignal (baseFs);
    juce::WavAudioFormat wav;

    auto write = [&] (const juce::File& f, const Buffer& data) {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        auto writer = wav.createWriterFor (os, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (baseFs)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
        if (writer == nullptr) { std::printf ("cannot write %s\n", f.getFullPathName().toRawUTF8()); return; }
        const float* chans[] = { data.data() };
        writer->writeFromFloatArrays (chans, 1, (int) data.size());
    };

    write (dir.getChildFile ("00_dry.wav"), in);
    int files = 1;
    for (int s = 0; s < getNumStyles(); ++s)
        for (float d : { 0.0f, 12.0f, 24.0f, 36.0f })
        {
            const auto out = runRoundTrip (s, baseFs, 2, d, true, false, in);
            const auto name = juce::String (s + 1).paddedLeft ('0', 2) + "_" + juce::String (nm (s)).replaceCharacter (' ', '_')
                            + "_drive" + juce::String ((int) d) + ".wav";
            write (dir.getChildFile (name), out);
            ++files;
        }
    std::printf ("wrote %d files (48 kHz / 24-bit mono, auto gain on, 2x oversampled) to %s\n", files, dir.getFullPathName().toRawUTF8());
    return 0;
}

} // namespace

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::String mode = argc > 1 ? argv[1] : "--test";
    const juce::String arg = argc > 2 ? argv[2] : "";

    if (mode == "--test")      return runTests();
    if (mode == "--calibrate") return runCalibrate (arg == "--write" ? juce::String (STSAT_CALIBRATION_FILE) : arg);
    if (mode == "--render")    return runRender (arg.isNotEmpty() ? arg : juce::File::getCurrentWorkingDirectory().getChildFile ("stsat-renders").getFullPathName());
    if (mode == "--harmonics") return runHarmonics();
    if (mode == "--spectrum")  return runSpectrum();
    if (mode == "--bench")     return runBench();

    std::printf ("usage: %s [--test | --calibrate [file|--write] | --render <dir> | --harmonics | --spectrum | --bench]\n", argv[0]);
    return 2;
}
