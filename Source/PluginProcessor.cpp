#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

using namespace stsat;

namespace
{
    constexpr int kChunk = 64;   // modulation / parameter update granularity (base-rate samples)
}

StSatProcessor::StSatProcessor()
    : ProcessorBase (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true),
                     createParameterLayout (getModConfig())),
      modMatrix (apvts, getModConfig())
{
    for (auto& id : getModulatableParameterIds())
        targetIndex[id] = modMatrix.addTarget (id);

    bind (inGain, ids::inputGain);
    bind (outGain, ids::outputGain);
    bind (mix, ids::mix);
    bind (outPan, ids::outputPan);
    bind (numBands, ids::numBands);
    bind (slope, ids::slope);
    bind (quality, ids::quality);
    bind (autoGain, ids::autoGain);
    for (int i = 0; i < kMaxCrossovers; ++i)
        bind (crossovers[(size_t) i], ids::crossover (i));

    for (int b = 0; b < kMaxBands; ++b)
    {
        auto& r = bandRefs[(size_t) b];
        auto id = [b] (const char* s) { return ids::band (b, s); };
        bind (r.style, id ("style"));             bind (r.drive, id ("drive"));
        bind (r.dyn, id ("dyn"));                 bind (r.upT, id ("dyn_up_thresh"));
        bind (r.upR, id ("dyn_up_ratio"));        bind (r.downT, id ("dyn_down_thresh"));
        bind (r.downR, id ("dyn_down_ratio"));    bind (r.attack, id ("dyn_attack"));
        bind (r.release, id ("dyn_release"));     bind (r.dynPos, id ("dyn_pos"));
        bind (r.toneLow, id ("tone_low"));        bind (r.toneLmid, id ("tone_lmid"));
        bind (r.toneHmid, id ("tone_hmid"));      bind (r.toneHigh, id ("tone_high"));
        bind (r.fb, id ("fb"));
        bind (r.fbFreq, id ("fb_freq"));          bind (r.mix, id ("mix"));
        bind (r.level, id ("level"));             bind (r.pan, id ("pan"));
        bind (r.chan, id ("chan"));               bind (r.mute, id ("mute"));
        bind (r.solo, id ("solo"));               bind (r.bypass, id ("bypass"));
    }

    engine.onBandSplit = [this] (int band, const juce::AudioBuffer<float>& b, int osN)
    {
        // Decimate the band to the base rate for the envelope followers (level detection only).
        const int factor = engine.getOversamplingFactor();
        auto& dest = bandEnvBuffers[(size_t) band];
        const int n = juce::jmin (dest.getNumSamples(), osN / factor);
        for (int c = 0; c < juce::jmin (b.getNumChannels(), dest.getNumChannels()); ++c)
        {
            const float* src = b.getReadPointer (c);
            float* d = dest.getWritePointer (c);
            for (int i = 0; i < n; ++i) d[i] = src[i * factor];
        }
        bandEnvCount[(size_t) band] = n;
    };

    registerFactoryPresets (presets);
}

StSatProcessor::~StSatProcessor() = default;

void StSatProcessor::bind (MRef& r, const juce::String& id)
{
    r.bind (apvts, id);
    auto it = targetIndex.find (id);
    r.target = it != targetIndex.end() ? it->second : -1;
}

float StSatProcessor::value (const MRef& r) const noexcept
{
    const float plain = r.get();
    if (r.target < 0 || ! modMatrix.isTargetModulated (r.target))
        return plain;

    const float base = r.param->convertTo0to1 (plain);
    return r.param->convertFrom0to1 (modMatrix.getModulatedNormalised (r.target, base));
}

//==============================================================================
void StSatProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    prepareShell (sampleRate);

    const int channels = juce::jmax (1, getTotalNumOutputChannels());
    engine.prepare (sampleRate, kChunk, channels);
    modMatrix.prepare (sampleRate, kChunk);

    for (auto& b : bandEnvBuffers)
        b.setSize (channels, kChunk, false, true, true);
    bandEnvCount.fill (0);

    updateEngineSettings();
    engine.reset();

    lastLatency = engine.getLatencySamples();
    setLatencySamples (lastLatency);
    juce::ignoreUnused (samplesPerBlock);
}

void StSatProcessor::updateEngineSettings() noexcept
{
    Engine::GlobalSettings g;
    g.numBands = juce::jlimit (1, kMaxBands, numBands.getInt());
    g.slope = (st::dsp::Crossover::Slope) juce::jlimit (0, 2, slope.getInt());
    g.qualityIndex = juce::jlimit (0, 3, quality.getInt());
    g.inputGainDb = value (inGain);
    g.outputGainDb = value (outGain);
    g.mix = value (mix) * 0.01f;
    g.outputPan = value (outPan) * 0.01f;
    for (int i = 0; i < kMaxCrossovers; ++i)
        g.crossovers[(size_t) i] = value (crossovers[(size_t) i]);
    engine.setGlobal (g);

    bool anySolo = false;
    for (int b = 0; b < g.numBands; ++b)
        anySolo |= bandRefs[(size_t) b].solo.getBool();

    const bool ag = autoGain.getBool();

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& r = bandRefs[(size_t) b];
        BandSettings s;
        s.style = r.style.getInt();
        s.driveDb = value (r.drive);
        s.dynamics = value (r.dyn) * 0.01f;
        s.upThresholdDb = value (r.upT);
        s.upRatio = value (r.upR);
        s.downThresholdDb = value (r.downT);
        s.downRatio = value (r.downR);
        s.attackMs = value (r.attack);
        s.releaseMs = value (r.release);
        s.dynPosition = r.dynPos.getInt() == 0 ? DynamicsPosition::pre : DynamicsPosition::post;
        s.toneDb = { value (r.toneLow), value (r.toneLmid), value (r.toneHmid), value (r.toneHigh) };
        s.feedback = value (r.fb) * 0.01f;
        s.feedbackHz = value (r.fbFreq);
        s.mix = value (r.mix) * 0.01f;
        s.levelDb = value (r.level);
        s.pan = value (r.pan) * 0.01f;
        s.channels = (ChannelMode) juce::jlimit (0, 4, r.chan.getInt());
        s.bypass = r.bypass.getBool();
        s.audible = (r.mute.getBool() || (anySolo && ! r.solo.getBool())) ? 0.0f : 1.0f;
        s.autoGain = ag;
        engine.setBand (b, s);
    }
}

void StSatProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();

    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, total);

    const int channels = juce::jmin (2, numOut);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            modMatrix.setTransport (*pos);

    inputMeter.push (buffer, total);
    preAnalyzer.push (buffer, total);

    for (int start = 0; start < total; start += kChunk)
    {
        const int n = juce::jmin (kChunk, total - start);
        float* ptrs[2] = { buffer.getWritePointer (0, start), channels > 1 ? buffer.getWritePointer (1, start) : nullptr };
        juce::AudioBuffer<float> chunk (ptrs, channels, n);

        // Envelope followers: input is fed for this chunk, bands from the previous chunk.
        for (int e = 0; e < getModConfig().numEnvelopes; ++e)
        {
            const int src = modMatrix.getEnvelopeInputChoice (e);
            if (src == 0)
                modMatrix.feedEnvelopeInput (e, chunk.getArrayOfReadPointers(), channels, n);
            else if (src - 1 < kMaxBands && bandEnvCount[(size_t) (src - 1)] > 0)
            {
                auto& bb = bandEnvBuffers[(size_t) (src - 1)];
                modMatrix.feedEnvelopeInput (e, bb.getArrayOfReadPointers(), channels, bandEnvCount[(size_t) (src - 1)]);
            }
        }
        modMatrix.advance (n);

        updateEngineSettings();
        engine.process (chunk, n);
    }

    if (engine.getLatencySamples() != lastLatency)
    {
        lastLatency = engine.getLatencySamples();
        setLatencySamples (lastLatency);
    }

    outputMeter.push (buffer, total);
    postAnalyzer.push (buffer, total);
}

//==============================================================================
void StSatProcessor::setParamPlain (const juce::String& id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void StSatProcessor::copyBand (int from, int to)
{
    for (auto& suffix : ids::bandSuffixes)
    {
        auto* src = apvts.getParameter (ids::band (from, suffix));
        auto* dst = apvts.getParameter (ids::band (to, suffix));
        dst->beginChangeGesture();
        dst->setValueNotifyingHost (src->getValue());
        dst->endChangeGesture();
    }
}

void StSatProcessor::remapBandRoutings (const std::function<int (int)>& mapBand)
{
    const auto routings = modMatrix.getRoutings();
    std::vector<st::mod::Routing> moved;

    for (const auto& r : routings)
    {
        // Band parameters look like "b<n>_<suffix>"
        if (! r.targetId.startsWithChar ('b') || ! r.targetId.containsChar ('_')) continue;
        const int oldBand = r.targetId.substring (1).getIntValue() - 1;
        if (! juce::isPositiveAndBelow (oldBand, kMaxBands)) continue;

        const int newBand = mapBand (oldBand);
        if (newBand == oldBand) continue;

        modMatrix.removeRouting (r.source, r.targetId);
        if (newBand >= 0)
        {
            auto copy = r;
            copy.targetId = ids::band (newBand, r.targetId.fromFirstOccurrenceOf ("_", false, false));
            moved.push_back (copy);
        }
    }

    for (const auto& r : moved)
    {
        modMatrix.addRouting (r.source, r.targetId, r.depth);
        if (! r.enabled) modMatrix.setRoutingEnabled (r.source, r.targetId, false);
    }
}

void StSatProcessor::setCrossoverOrdered (int index, float hz, bool asGesture)
{
    const int nb = getNumBands();
    if (! juce::isPositiveAndBelow (index, nb - 1)) return;

    constexpr float minRatio = 1.18f;
    const float lo = index > 0 ? apvts.getRawParameterValue (ids::crossover (index - 1))->load() * minRatio : 20.0f;
    const float hi = index < nb - 2 ? apvts.getRawParameterValue (ids::crossover (index + 1))->load() / minRatio : 20000.0f;
    hz = juce::jlimit (lo, juce::jmax (lo, hi), hz);

    auto* p = apvts.getParameter (ids::crossover (index));
    if (asGesture) p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (hz));
    if (asGesture) p->endChangeGesture();
}

void StSatProcessor::addBand (float hz)
{
    const int nb = getNumBands();
    if (nb >= kMaxBands) return;

    undoManager.beginNewTransaction ("Add band");

    std::vector<float> xo;
    for (int i = 0; i < nb - 1; ++i)
        xo.push_back (apvts.getRawParameterValue (ids::crossover (i))->load());

    // Which band does the new split fall into?
    int k = 0;
    while (k < (int) xo.size() && hz > xo[(size_t) k]) ++k;

    // Keep a sensible spacing from neighbours.
    const float lo = k > 0 ? xo[(size_t) k - 1] * 1.2f : 25.0f;
    const float hi = k < (int) xo.size() ? xo[(size_t) k] / 1.2f : 18000.0f;
    if (lo >= hi) return;
    hz = juce::jlimit (lo, hi, hz);

    // Shift bands above k up by one, duplicate band k into k+1.
    for (int b = nb - 1; b > k; --b)
        copyBand (b, b + 1);
    copyBand (k, k + 1);

    xo.insert (xo.begin() + k, hz);
    for (int i = 0; i < (int) xo.size(); ++i)
        setParamPlain (ids::crossover (i), xo[(size_t) i]);

    setParamPlain (ids::numBands, (float) (nb + 1));

    // Bands above k moved up by one; the new band k+1 starts without modulation.
    remapBandRoutings ([k] (int b) { return b > k ? b + 1 : b; });
}

void StSatProcessor::removeBand (int k)
{
    const int nb = getNumBands();
    if (nb <= 1 || ! juce::isPositiveAndBelow (k, nb)) return;

    undoManager.beginNewTransaction ("Remove band");

    std::vector<float> xo;
    for (int i = 0; i < nb - 1; ++i)
        xo.push_back (apvts.getRawParameterValue (ids::crossover (i))->load());

    for (int b = k; b < nb - 1; ++b)
        copyBand (b + 1, b);

    // Remove the crossover that formed this band's upper edge (or the lower one for the top band).
    xo.erase (xo.begin() + juce::jmin (k, (int) xo.size() - 1));
    for (int i = 0; i < (int) xo.size(); ++i)
        setParamPlain (ids::crossover (i), xo[(size_t) i]);

    // Reset the now-unused top band so it starts clean if re-added.
    for (auto& suffix : ids::bandSuffixes)
        if (auto* p = apvts.getParameter (ids::band (nb - 1, suffix)))
            p->setValueNotifyingHost (p->getDefaultValue());

    setParamPlain (ids::numBands, (float) (nb - 1));

    remapBandRoutings ([k] (int b) { return b == k ? -1 : (b > k ? b - 1 : b); });
}

void StSatProcessor::stateRestored() {}

juce::AudioProcessorEditor* StSatProcessor::createEditor()
{
    return new StSatEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StSatProcessor();
}
