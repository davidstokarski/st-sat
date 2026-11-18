#include "Saturation.h"
#include "SaturationStyles.h"
#include "SaturationCalibration.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace stsat
{

//==============================================================================
// Style catalogue (index order is the parameter order - append only!)

static const StyleInfo kStyles[] = {
    { "Clean Triode",     "Tube",       "Single triode stage: transparent, mostly 2nd harmonic, gently rounds peaks." },
    { "Warm Valve",       "Tube",       "Two cascaded triodes with low-end emphasis: thick 2nd/3rd, softened top." },
    { "Pushed Valve",     "Tube",       "Hot triode pair with grid blocking bias shift and supply sag: blooms and compresses." },
    { "Glass Radio",      "Tube",       "Band-limited, nasal midrange through a small tube output stage." },
    { "Pentode Edge",     "Tube",       "Pentode-style sharper knee with screen compression: forward, biting upper mids." },
    { "Soft Tape",        "Tape",       "Pre/de-emphasised tape with light hysteresis and head bump: smooth, glued highs." },
    { "Hot Tape",         "Tape",       "Driven tape: deeper head bump, hysteresis memory, level-dependent HF compression." },
    { "Worn Cassette",    "Tape",       "Narrow cassette band, heavy HF squash and oxide grit - no wow, no hiss." },
    { "Gentle Glow",      "Saturation", "Ultra-soft root-law curve that never flattens: subtle density and sheen." },
    { "Dense",            "Saturation", "Three soft stages with slow glue compression: thick, dense and forward." },
    { "Iron Transformer", "Saturation", "Low-frequency-weighted core saturation with remanence: weighty, tight lows." },
    { "Console Bus",      "Saturation", "Class-A line amp with soft slew limiting and rail clipping: punchy, sticky." },
    { "Clean Combo",      "Amp",        "Bright clean amp channel with push-pull power stage; breaks up only when pushed." },
    { "Crunch Stack",     "Amp",        "Mid-pushed two-stage preamp into a sagging power amp: classic crunch." },
    { "Lead Stack",       "Amp",        "Three-stage high-gain preamp, smooth top and sustaining compression." },
    { "Bass Rig",         "Amp",        "Split bass amp: round tube lows, growling driven mids." },
    { "Fuzz Pedal",       "Amp",        "Two-transistor fuzz with starved bias: square, spitty, gated tails." },
    { "Soft Clip",        "Clip",       "Wide-knee symmetric clipper: rounds peaks, odd harmonics." },
    { "Hard Clip",        "Clip",       "Transparent until the ceiling, then brick-wall clipping." },
    { "Diode Pair",       "Clip",       "Asymmetric diode clipper: logarithmic, never quite flat, even + odd." },
    { "Silicon",          "Clip",       "Two transistor stages with exponential knees and hard rails: crisp and bright." },
    { "Full Wave",        "FX",         "Full-wave rectifier: octave-up, hollow, aggressive." },
    { "Half Wave",        "FX",         "Half-wave rectifier: strong even harmonics, buzzy and lopsided." },
    { "Wavefold",         "FX",         "Sine wavefolder: folds peaks back on themselves into rich metallic overtones." },
    { "Sputter",          "FX",         "Envelope-tracking dead zone: crackles, sputters and gates as notes decay." },
    { "Smear",            "FX",         "Envelope and LF-driven bias with dynamic smudge filter: blurred, intermodulated." },
    { "Rate Crush",       "FX",         "Sample-and-hold relative to the host rate plus bit reduction." },
};

static_assert (std::size (kStyles) == (size_t) detail::kNumStylesInternal);
static_assert (calib::kNumStyles == detail::kNumStylesInternal);

int getNumStyles() noexcept { return (int) std::size (kStyles); }

const StyleInfo& getStyleInfo (int index) noexcept
{
    return kStyles[(size_t) juce::jlimit (0, getNumStyles() - 1, index)];
}

juce::StringArray getStyleNames()
{
    juce::StringArray names;
    for (auto& s : kStyles)
        names.add (s.name);
    return names;
}

juce::StringArray getStyleCategories()
{
    juce::StringArray cats;
    for (auto& s : kStyles)
        cats.addIfNotAlreadyThere (s.category);
    return cats;
}

//==============================================================================
namespace
{
    inline float dbToGain (float db) noexcept { return std::exp (db * 0.1151292546f); }

    /** Auto-gain compensation in dB for style s at drive dB (linear interpolation of the table). */
    inline float autoGainDb (int s, float driveDb) noexcept
    {
        const float pos = juce::jlimit (0.0f, (float) (calib::kNumDrivePoints - 1), driveDb / calib::kDriveStepDb);
        const int i0 = juce::jmin ((int) pos, calib::kNumDrivePoints - 2);
        const float f = pos - (float) i0;
        return calib::kAutoGainDb[s][i0] + f * (calib::kAutoGainDb[s][i0 + 1] - calib::kAutoGainDb[s][i0]);
    }

    inline detail::FrameCtx makeCtx (float dB) noexcept
    {
        detail::FrameCtx c;
        c.dB = dB;
        c.G = dbToGain (dB);
        c.t = juce::jlimit (0.0f, 1.0f, dB / Saturator::maxDriveDb);
        return c;
    }
}

struct Saturator::Impl
{
    detail::Setup setup;
    int numChannels = 0;
    std::vector<detail::StyleBank> banks;   // one bank of all styles per channel
    std::vector<detail::OnePole> dcBlock;   // final DC removal per channel

    int current = 0, previous = 0;
    bool fading = false;
    float fadePos = 0.0f, fadeInc = 1.0f;

    double driveDb = 0.0, driveCoef = 1.0;
    detail::FrameCtx ctx = makeCtx (0.0f);
    float autoMix = 0.0f, autoCoef = 1.0f;
    int osFactor = 1;
    float antiDenormal = 1.0e-20f;   // -400 dB, sign alternates every frame: keeps filter states out of denormals

    float styleGain (int s, float dB, bool bypass) const noexcept
    {
        if (bypass)
            return 1.0f;
        return dbToGain (calib::kUnityTrimDb[s] + autoMix * autoGainDb (s, dB));
    }
};

Saturator::Saturator() : impl (std::make_unique<Impl>()) {}
Saturator::~Saturator() = default;

Saturator::Saturator (Saturator&& o) noexcept
    : impl (std::move (o.impl)),
      requestedStyle (o.requestedStyle.load()),
      targetDriveDb (o.targetDriveDb.load()),
      autoGain (o.autoGain.load()),
      oversamplingFactor (o.oversamplingFactor.load()),
      calibrationBypass (o.calibrationBypass.load())
{
}

Saturator& Saturator::operator= (Saturator&& o) noexcept
{
    impl = std::move (o.impl);
    requestedStyle.store (o.requestedStyle.load());
    targetDriveDb.store (o.targetDriveDb.load());
    autoGain.store (o.autoGain.load());
    oversamplingFactor.store (o.oversamplingFactor.load());
    calibrationBypass.store (o.calibrationBypass.load());
    return *this;
}

void Saturator::prepare (double sampleRate, int, int numChannels)
{
    auto& d = *impl;
    d.osFactor = juce::jmax (1, oversamplingFactor.load());
    d.setup.fs = sampleRate;
    d.setup.baseFs = sampleRate / (double) d.osFactor;
    d.numChannels = juce::jmax (1, numChannels);

    d.banks.clear();
    d.banks.resize ((size_t) d.numChannels);   // value-initialised in place (a bank is ~10 kB)
    d.dcBlock.assign ((size_t) d.numChannels, detail::OnePole {});

    for (auto& b : d.banks)
        b.prepare (d.setup);
    for (auto& f : d.dcBlock)
        f.setCutoff (5.0, sampleRate);

    d.fadeInc = (float) (1.0 / (crossfadeSeconds * sampleRate));
    d.driveCoef = 1.0 - std::exp (-1.0 / (0.25 * driveSmoothingSeconds * sampleRate));   // ~98% settled in 20 ms
    d.autoCoef = (float) (1.0 - std::exp (-1.0 / (0.25 * driveSmoothingSeconds * sampleRate)));

    reset();
}

void Saturator::reset()
{
    auto& d = *impl;
    for (auto& b : d.banks)
        b.resetAll();
    for (auto& f : d.dcBlock)
        f.reset();

    d.current = d.previous = juce::jlimit (0, getNumStyles() - 1, requestedStyle.load());
    d.fading = false;
    d.fadePos = 0.0f;
    d.driveDb = targetDriveDb.load();
    const auto v = d.ctx.version;
    d.ctx = makeCtx ((float) d.driveDb);
    d.ctx.version = v + 1;
    d.autoMix = autoGain.load() ? 1.0f : 0.0f;
}

void Saturator::setStyle (int index)         { requestedStyle.store (juce::jlimit (0, getNumStyles() - 1, index)); }
void Saturator::setDrive (float driveDb)     { targetDriveDb.store (juce::jlimit (0.0f, maxDriveDb, driveDb)); }
void Saturator::setAutoGain (bool on)        { autoGain.store (on); }
void Saturator::setLatencyAlignedOversampling (int factor) { oversamplingFactor.store (juce::jmax (1, factor)); }
bool Saturator::isCrossfading() const noexcept { return impl->fading; }

void Saturator::processFrame (float* frame, int numChannels) noexcept
{
    auto& d = *impl;
    const int n = juce::jmin (numChannels, d.numChannels);
    if (n <= 0)
        return;

    // --- oversampling hint (rate-based styles) ---
    if (const int os = oversamplingFactor.load (std::memory_order_relaxed); os != d.osFactor)
    {
        d.osFactor = os;
        d.setup.baseFs = d.setup.fs / (double) os;
        for (auto& b : d.banks)
            b.s26.setBaseRate (d.setup.baseFs);
    }

    // --- style switching: start a crossfade once the previous one finished ---
    if (! d.fading)
    {
        const int req = requestedStyle.load (std::memory_order_relaxed);
        if (req != d.current)
        {
            d.previous = d.current;
            d.current = req;
            for (int ch = 0; ch < d.numChannels; ++ch)
                d.banks[(size_t) ch].reset (req);
            d.fading = true;
            d.fadePos = 0.0f;
        }
    }

    // --- drive & auto-gain smoothing (once per frame) ---
    const double target = targetDriveDb.load (std::memory_order_relaxed);
    if (const double diff = target - d.driveDb; std::abs (diff) > 0.0)
    {
        d.driveDb = std::abs (diff) < 1.0e-4 ? target : d.driveDb + diff * d.driveCoef;
        const auto v = d.ctx.version;
        d.ctx = makeCtx ((float) d.driveDb);
        d.ctx.version = v + 1;
    }

    const float autoTarget = autoGain.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    if (const float diff = autoTarget - d.autoMix; std::abs (diff) > 0.0f)
        d.autoMix = std::abs (diff) < 1.0e-5f ? autoTarget : d.autoMix + diff * d.autoCoef;

    const bool bypass = calibrationBypass.load (std::memory_order_relaxed);
    const auto& c = d.ctx;
    float gNew = d.styleGain (d.current, c.dB, bypass);
    float gOld = 0.0f;

    if (d.fading)
    {
        // compromise law between equal-power and linear: +1.5 dB worst case for fully
        // correlated styles, -1.5 dB for uncorrelated ones (styles are mostly correlated)
        const float p = d.fadePos;
        const float a = p * juce::MathConstants<float>::halfPi;
        gOld = d.styleGain (d.previous, c.dB, bypass) * std::sqrt ((1.0f - p) * std::cos (a));
        gNew *= std::sqrt (p * std::sin (a));
    }

    for (int ch = 0; ch < n; ++ch)
    {
        auto& bank = d.banks[(size_t) ch];
        const float x = frame[ch] + d.antiDenormal;
        float y = bank.process (d.current, x, c) * gNew;
        if (d.fading)
            y += bank.process (d.previous, x, c) * gOld;

        y = (float) d.dcBlock[(size_t) ch].hp (y);

        if (! (std::abs (y) < 1.0e3f))   // also catches NaN: recover rather than latch garbage
        {
            bank.resetAll();
            d.dcBlock[(size_t) ch].reset();
            y = 0.0f;
        }

        frame[ch] = y;
    }

    d.antiDenormal = -d.antiDenormal;

    if (d.fading)
    {
        d.fadePos += d.fadeInc;
        if (d.fadePos >= 1.0f)
        {
            d.fading = false;
            d.fadePos = 0.0f;
        }
    }
}

void Saturator::processBlock (float* const* channels, int numChannels, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;
    constexpr int maxCh = 16;
    float frame[maxCh];
    const int n = juce::jmin (numChannels, maxCh);

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < n; ++ch)
            frame[ch] = channels[ch][i];
        processFrame (frame, n);
        for (int ch = 0; ch < n; ++ch)
            channels[ch][i] = frame[ch];
    }
}

float Saturator::getTransferCurveSample (float input) const noexcept
{
    const int s = requestedStyle.load (std::memory_order_relaxed);
    const float dB = targetDriveDb.load (std::memory_order_relaxed);
    const auto c = makeCtx (dB);
    const float y = detail::StyleBank::curve (s, input, c);

    if (calibrationBypass.load (std::memory_order_relaxed))
        return y;

    const float trim = calib::kUnityTrimDb[s] + (autoGain.load (std::memory_order_relaxed) ? autoGainDb (s, dB) : 0.0f);
    return y * dbToGain (trim);
}

} // namespace stsat
