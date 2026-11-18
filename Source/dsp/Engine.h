#pragma once
#include "../Params.h"
#include "Saturation.h"

namespace stsat
{

/** Everything a band needs for one processing chunk (plain parameter values, post-modulation). */
struct BandSettings
{
    int style = 0;
    float driveDb = 0.0f;
    float dynamics = 0.0f;          // -1..+1
    float upThresholdDb = -36.0f, upRatio = 3.0f;
    float downThresholdDb = -18.0f, downRatio = 4.0f;
    float attackMs = 8.0f, releaseMs = 120.0f;
    DynamicsPosition dynPosition = DynamicsPosition::post;
    std::array<float, 4> toneDb {};  // low shelf, low-mid, high-mid, high shelf
    float feedback = 0.0f;          // 0..1
    float feedbackHz = 1000.0f;
    float mix = 1.0f;               // 0..1
    float levelDb = 0.0f;
    float pan = 0.0f;               // -1..+1
    ChannelMode channels = ChannelMode::stereo;
    bool bypass = false;
    float audible = 1.0f;           // 0 = muted (mute / solo logic), 1 = audible
    bool autoGain = true;
};

/** One band of the multiband engine. Runs at the oversampled rate. */
class BandProcessor
{
public:
    void prepare (double oversampledRate, int maxBlock, int numChannels, int oversamplingFactor);
    void reset();
    void setOversamplingFactor (int factor);

    void setSettings (const BandSettings&) noexcept;
    /** In place. `numSamples` at the oversampled rate. */
    void process (juce::AudioBuffer<float>& band, int numSamples) noexcept;

    // UI feeds
    std::atomic<float> outputLevelDb { -100.0f };
    std::atomic<float> gainChangeDb { 0.0f };
    std::atomic<float> detectorDb { -100.0f };

    const Saturator& getSaturator() const noexcept { return saturator; }

private:
    BandSettings settings;
    int numCh = 2, maxBlockSize = 0;
    double fs = 96000.0;

    Saturator saturator;
    std::array<st::dsp::Biquad, 4> toneEq;
    std::array<float, 4> toneApplied { 99.0f, 99.0f, 99.0f, 99.0f };
    bool toneActive = false;
    void updateTone();
    st::dsp::Svf feedbackFilter;
    st::dsp::UpDownCompressor dynamics;
    juce::AudioBuffer<float> dry;

    st::dsp::LinearSmoother feedbackGain, wet, gainL, gainR, bypassMix, audibleGain;
    std::array<float, 2> feedbackState {};
    ChannelMode lastChannelMode = ChannelMode::stereo;
    float levelFollower = 0.0f, levelCoeff = 0.999f;

    void processActiveChannels (juce::AudioBuffer<float>& band, int numSamples, int firstCh, int numActive) noexcept;
};

//==============================================================================
/** Complete DSP graph (independent of AudioProcessor so it can be tested offline). */
class Engine
{
public:
    struct GlobalSettings
    {
        int numBands = 1;
        std::array<float, kMaxCrossovers> crossovers { defaults::crossovers[0], defaults::crossovers[1], defaults::crossovers[2],
                                                       defaults::crossovers[3], defaults::crossovers[4] };
        st::dsp::Crossover::Slope slope = st::dsp::Crossover::Slope::db24;
        int qualityIndex = 1;   // 0..3 -> 1x..8x
        float inputGainDb = 0.0f, outputGainDb = 0.0f, mix = 1.0f, outputPan = 0.0f;
    };

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Call before process(); cheap. Settings for all kMaxBands are expected. */
    void setGlobal (const GlobalSettings&) noexcept;
    void setBand (int band, const BandSettings&) noexcept;

    /** Processes `numSamples` (<= maxBlockSize) in place. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

    int getLatencySamples() const noexcept { return oversampler.getLatencySamples(); }
    int getLatencyForQuality (int q) const noexcept { return oversampler.getLatencySamples (q); }
    int getOversamplingFactor() const noexcept { return oversampler.getFactor(); }

    BandProcessor& getBand (int i) noexcept { return bands[(size_t) i]; }
    const st::dsp::Crossover& getCrossover() const noexcept { return crossover; }

    /** Optional hook: called per band with the band buffer at the base rate? No — oversampled rate,
        after the crossover split and before band processing (used to feed envelope followers). */
    std::function<void (int band, const juce::AudioBuffer<float>&, int numSamples)> onBandSplit;

private:
    GlobalSettings global;
    std::array<BandSettings, kMaxBands> bandSettings;
    std::array<BandProcessor, kMaxBands> bands;

    st::dsp::Oversampler oversampler;
    st::dsp::Crossover crossover;
    st::dsp::DelayLine dryDelay;
    std::array<juce::AudioBuffer<float>, st::dsp::Crossover::kMaxBands> bandBuffers;
    juce::AudioBuffer<float> osInput, dryBuffer;

    st::dsp::LinearSmoother inGain, outGain, mixSmoother, panL, panR;
    double baseRate = 48000.0;
    int maxBlock = 0, numCh = 2, currentFactor = -1;

    void applyQuality();
};

} // namespace stsat
