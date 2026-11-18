#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

namespace stsat
{

struct StyleInfo
{
    const char* name;
    const char* category;
    const char* description;
};

int getNumStyles() noexcept;
const StyleInfo& getStyleInfo (int index) noexcept;
juce::StringArray getStyleNames();          // for AudioParameterChoice (index order)
juce::StringArray getStyleCategories();     // ordered unique categories, for a categorised menu

/**
    Per-band drive + saturation-style processor.

    Runs at the oversampled rate, one sample frame at a time (so it can sit inside a
    per-sample feedback loop). Real-time safe: all state is allocated in prepare().
    Parameter setters are lock-free (atomics) and may be called from any thread.

    Denormals: a -400 dB alternating offset keeps all filter states normal even without
    FTZ; holding a juce::ScopedNoDenormals around processing is still recommended.
*/
class Saturator
{
public:
    Saturator();
    ~Saturator();
    Saturator (Saturator&&) noexcept;
    Saturator& operator= (Saturator&&) noexcept;

    void prepare (double sampleRate, int maxBlockSize, int numChannels);  // sampleRate = oversampled rate
    void reset();

    void setStyle (int index);          // click-free ~20 ms crossfade between old and new style
    void setDrive (float driveDb);      // 0..+48 dB, smoothed internally (~20 ms)
    void setAutoGain (bool);            // calibrated make-up so loudness stays ~constant vs drive
    void setLatencyAlignedOversampling (int factor);  // 1, 2, 4, 8: lets rate-based styles find the base rate

    /** Process one sample frame (all channels at the same time index), in place. */
    void processFrame (float* frame, int numChannels) noexcept;

    /** Equivalent to calling processFrame() for every frame. */
    void processBlock (float* const* channels, int numChannels, int numSamples) noexcept;

    /** UI: static transfer curve of the current style at the current drive (stateless approximation). */
    float getTransferCurveSample (float input) const noexcept;

    //==============================================================================
    // Extras (diagnostics / calibration)
    int getStyle() const noexcept { return requestedStyle.load (std::memory_order_relaxed); }
    float getDrive() const noexcept { return targetDriveDb.load (std::memory_order_relaxed); }
    bool getAutoGain() const noexcept { return autoGain.load (std::memory_order_relaxed); }
    bool isCrossfading() const noexcept;

    /** Calibration tool only: ignore the baked unity trims and auto-gain tables. */
    void setCalibrationBypass (bool shouldBypass) noexcept { calibrationBypass.store (shouldBypass); }

    static constexpr float maxDriveDb = 48.0f;
    static constexpr double crossfadeSeconds = 0.02;
    static constexpr double driveSmoothingSeconds = 0.02;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    std::atomic<int> requestedStyle { 0 };
    std::atomic<float> targetDriveDb { 0.0f };
    std::atomic<bool> autoGain { false };
    std::atomic<int> oversamplingFactor { 1 };
    std::atomic<bool> calibrationBypass { false };
};

} // namespace stsat
