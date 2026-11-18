#pragma once
#include "Params.h"
#include "dsp/Engine.h"

class StSatProcessor : public st::ProcessorBase
{
public:
    StSatProcessor();
    ~StSatProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    double getTailLengthSeconds() const override { return 0.05; }

    //==============================================================================
    st::mod::ModMatrix modMatrix;
    stsat::Engine& getEngine() noexcept { return engine; }
    int getNumBands() const noexcept { return numBands.getInt(); }

    /** Message-thread band editing (undoable). */
    void addBand (float atFrequencyHz);
    void removeBand (int bandIndex);
    /** Move crossover `index` keeping all crossovers ordered with a minimum spacing. */
    void setCrossoverOrdered (int index, float hz, bool asGesture);

    juce::RangedAudioParameter& param (const juce::String& id) { return *apvts.getParameter (id); }
    juce::RangedAudioParameter& bandParam (int band, const juce::String& suffix) { return *apvts.getParameter (stsat::ids::band (band, suffix)); }

    void stateRestored() override;

private:
    stsat::Engine engine;

    /** Parameter reference that knows its modulation target index (-1 = not modulatable). */
    struct MRef : st::params::Ref { int target = -1; };

    // Cached parameters
    MRef inGain, outGain, mix, outPan, numBands, slope, quality, autoGain;
    std::array<MRef, stsat::kMaxCrossovers> crossovers;

    struct BandRefs
    {
        MRef style, drive, driveSpread, dyn, upT, upR, downT, downR, attack, release, dynPos,
             toneLow, toneLmid, toneHmid, toneHigh, fb, fbFreq, mix, level, pan, chan, mute, solo, bypass,
             spType, spMix, spDecay, spSize, spPre, spTone, spWidth, spComp, spDuck, spDuckSrc, spDuckRel;
    };
    std::array<BandRefs, stsat::kMaxBands> bandRefs;
    std::unordered_map<juce::String, int> targetIndex;

    // Base-rate band signals for envelope followers
    std::array<juce::AudioBuffer<float>, stsat::kMaxBands> bandEnvBuffers;
    std::array<int, stsat::kMaxBands> bandEnvCount {};
    int lastLatency = -1;

    float value (const MRef&) const noexcept;
    void bind (MRef&, const juce::String& id);
    void updateEngineSettings() noexcept;

    void copyBand (int from, int to);
    /** Re-targets modulation routings after bands were shifted. mapBand(old) returns the new band index or -1 to drop. */
    void remapBandRoutings (const std::function<int (int)>& mapBand);
    void setParamPlain (const juce::String& id, float plain);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StSatProcessor)
};
