#include "Engine.h"

namespace stsat
{

using st::dsp::LinearSmoother;

//==============================================================================
void BandProcessor::prepare (double rate, int maxBlock, int channels, int factor)
{
    fs = rate;
    numCh = juce::jlimit (1, 2, channels);
    maxBlockSize = maxBlock;

    saturator.prepare (fs, maxBlock, numCh);
    saturator.setLatencyAlignedOversampling (factor);
    for (auto& f : toneEq) f.prepare (numCh);
    toneApplied.fill (99.0f);
    updateTone();
    feedbackFilter.prepare (fs, numCh);
    feedbackFilter.setParameters (st::dsp::SvfMode::bandPass, settings.feedbackHz, 2.2);
    dynamics.prepare (fs, maxBlock, numCh);
    dry.setSize (numCh, maxBlock, false, false, true);

    for (auto* s : { &feedbackGain, &wet, &gainL, &gainR, &bypassMix, &audibleGain, &spreadL, &spreadR })
        s->prepare (fs, 25.0);
    spreadL.setCurrentAndTarget (1.0f);
    spreadR.setCurrentAndTarget (1.0f);

    feedbackGain.setCurrentAndTarget (0.0f);
    wet.setCurrentAndTarget (settings.mix);
    gainL.setCurrentAndTarget (1.0f);
    gainR.setCurrentAndTarget (1.0f);
    bypassMix.setCurrentAndTarget (settings.bypass ? 0.0f : 1.0f);
    audibleGain.setCurrentAndTarget (settings.audible);

    levelCoeff = (float) std::exp (-1.0 / (fs * 0.08));

    reverb.prepare (fs, juce::jmax (1, factor), maxBlock);
    spaceComp.prepare (fs, maxBlock, 2);
    spaceWet.setSize (2, maxBlock, false, false, true);
    spaceMix.prepare (fs, 30.0);
    duckGain.prepare (fs, 8.0);
    spaceMix.setCurrentAndTarget (0.0f);
    duckGain.setCurrentAndTarget (1.0f);
    reset();
}

void BandProcessor::setOversamplingFactor (int factor)
{
    saturator.setLatencyAlignedOversampling (factor);
}

void BandProcessor::reset()
{
    saturator.reset();
    for (auto& f : toneEq) f.reset();
    feedbackFilter.reset();
    dynamics.reset();
    feedbackState.fill (0.0f);
    levelFollower = 0.0f;
    reverb.reset();
    spaceComp.reset();
    duckEnv = 0.0f;
    spaceRunning = false;
}

void BandProcessor::setSettings (const BandSettings& s) noexcept
{
    settings = s;

    saturator.setStyle (s.style);
    saturator.setDrive (s.driveDb);
    saturator.setAutoGain (s.autoGain);
    updateTone();
    feedbackFilter.setParameters (st::dsp::SvfMode::bandPass, juce::jlimit (20.0, fs * 0.45, (double) s.feedbackHz), 2.2);

    // Feedback: perceptually scaled, stays below self-oscillation runaway (the saturator bounds it anyway).
    feedbackGain.setTarget (0.92f * std::pow (s.feedback, 0.7f));
    spreadL.setTarget (juce::Decibels::decibelsToGain (-0.5f * s.driveSpreadDb));
    spreadR.setTarget (juce::Decibels::decibelsToGain (0.5f * s.driveSpreadDb));
    wet.setTarget (s.mix);
    bypassMix.setTarget (s.bypass ? 0.0f : 1.0f);
    audibleGain.setTarget (s.audible);

    const float level = juce::Decibels::decibelsToGain (s.levelDb, -100.0f);
    const float p = juce::jlimit (-1.0f, 1.0f, s.pan);
    gainL.setTarget (level * (p <= 0.0f ? 1.0f : std::cos (p * juce::MathConstants<float>::halfPi)));
    gainR.setTarget (level * (p >= 0.0f ? 1.0f : std::cos (-p * juce::MathConstants<float>::halfPi)));

    st::dsp::UpDownCompressor::Params dp;
    dp.depth = s.dynamics;
    dp.upThresholdDb = s.upThresholdDb;
    dp.upRatio = s.upRatio;
    dp.downThresholdDb = s.downThresholdDb;
    dp.downRatio = s.downRatio;
    dp.attackMs = s.attackMs;
    dp.releaseMs = s.releaseMs;
    dp.kneeDb = 6.0f;
    dp.stereoLink = 1.0f;
    dp.rmsBlend = 0.5f;
    dynamics.setParams (dp);

    {
        st::dsp::Reverb::Params rp;
        rp.type = (st::dsp::Reverb::Type) juce::jlimit (0, 7, s.space.type);
        rp.decaySeconds = s.space.decaySeconds;
        rp.size = s.space.size;
        rp.predelayMs = s.space.predelayMs;
        rp.tone = s.space.tone;
        rp.width = s.space.width;
        rp.modulation = 0.35f;
        reverb.setParams (rp);

        st::dsp::UpDownCompressor::Params cp;
        const float k = s.space.compression;
        cp.depth = k > 0.0005f ? 1.0f : 0.0f;
        cp.downThresholdDb = -10.0f - 26.0f * k;
        cp.downRatio = 1.0f + 9.0f * k;
        cp.upThresholdDb = -120.0f;
        cp.upRatio = 1.0f;
        cp.attackMs = 6.0f;
        cp.releaseMs = 140.0f;
        cp.kneeDb = 8.0f;
        spaceComp.setParams (cp);
    }

    if (s.channels != lastChannelMode)
    {
        // Avoid stale state from another channel assignment ringing through.
        saturator.reset();
        feedbackState.fill (0.0f);
        feedbackFilter.reset();
        lastChannelMode = s.channels;
    }
}

void BandProcessor::updateTone()
{
    toneActive = false;
    for (int k = 0; k < 4; ++k)
    {
        const float db = settings.toneDb[(size_t) k];
        toneActive |= std::abs (db) > 0.01f;
        if (std::abs (db - toneApplied[(size_t) k]) < 0.005f) continue;
        toneApplied[(size_t) k] = db;
        const auto type = k == 0 ? st::dsp::BiquadType::lowShelf : (k == 3 ? st::dsp::BiquadType::highShelf : st::dsp::BiquadType::peak);
        const double f = juce::jmin ((double) defaults::toneFreq[k], fs * 0.4);
        toneEq[(size_t) k].setParameters (type, fs, f, defaults::toneQ[k], db);
    }
}

void BandProcessor::processActiveChannels (juce::AudioBuffer<float>& band, int n, int firstCh, int numActive) noexcept
{
    float* ch[2] = { band.getWritePointer (firstCh), numActive > 1 ? band.getWritePointer (firstCh + 1) : nullptr };

    // A light-weight view over only the active channels (no allocation for <= 32 channels).
    juce::AudioBuffer<float> view (ch, numActive, n);

    const bool dynActive = std::abs (settings.dynamics) > 0.0005f;

    if (dynActive && settings.dynPosition == DynamicsPosition::pre)
        dynamics.process (view, n);

    // Saturation with resonant feedback loop (per frame).
    // Stereo drive spread: extra pre-gain per channel (only when both channels are processed);
    // undone after the saturator when auto gain is on, so it reads as "more/less drive", not level.
    const bool spread = numActive == 2;
    const bool undoSpread = settings.autoGain;

    float frame[2];
    for (int i = 0; i < n; ++i)
    {
        const float fbg = feedbackGain.getNext();
        const float sg[2] = { spreadL.getNext(), spreadR.getNext() };
        for (int c = 0; c < numActive; ++c)
            frame[c] = (ch[c][i] + fbg * feedbackState[(size_t) c]) * (spread ? sg[c] : 1.0f);

        saturator.processFrame (frame, numActive);

        if (spread && undoSpread)
            for (int c = 0; c < numActive; ++c)
                frame[c] /= sg[c];

        for (int c = 0; c < numActive; ++c)
        {
            feedbackState[(size_t) c] = st::dsp::flushDenormal (feedbackFilter.processSample (c, frame[c]));
            ch[c][i] = frame[c];
        }
    }

    // Tone tilt after the saturator (outside the feedback loop).
    if (toneActive)
        for (int c = 0; c < numActive; ++c)
            for (auto& f : toneEq)
                f.process (ch[c], n, c);

    if (dynActive && settings.dynPosition == DynamicsPosition::post)
        dynamics.process (view, n);

    if (dynActive)
    {
        gainChangeDb.store (dynamics.getGainChangeDb(), std::memory_order_relaxed);
        detectorDb.store (dynamics.getDetectorLevelDb(), std::memory_order_relaxed);
    }
    else
    {
        gainChangeDb.store (0.0f, std::memory_order_relaxed);
    }
}

void BandProcessor::process (juce::AudioBuffer<float>& band, int n) noexcept
{
    const int channels = juce::jmin (numCh, band.getNumChannels());

    // Keep the unprocessed band for band-mix / bypass crossfades.
    for (int c = 0; c < channels; ++c)
        dry.copyFrom (c, 0, band, c, 0, n);

    const bool fullyBypassed = ! bypassMix.isSmoothing() && bypassMix.getCurrent() <= 0.0f;

    if (! fullyBypassed)
    {
        const bool stereo = channels == 2;
        const auto mode = stereo ? settings.channels : ChannelMode::stereo;

        if (mode == ChannelMode::mid || mode == ChannelMode::side)
            st::dsp::encodeMidSide (band.getWritePointer (0), band.getWritePointer (1), n);
        if (mode == ChannelMode::mid || mode == ChannelMode::side)
            st::dsp::encodeMidSide (dry.getWritePointer (0), dry.getWritePointer (1), n);

        switch (mode)
        {
            case ChannelMode::stereo: processActiveChannels (band, n, 0, channels); break;
            case ChannelMode::mid:
            case ChannelMode::left:   processActiveChannels (band, n, 0, 1); break;
            case ChannelMode::side:
            case ChannelMode::right:  processActiveChannels (band, n, 1, 1); break;
        }

        // Band mix (in the processing domain), then back to L/R.
        for (int i = 0; i < n; ++i)
        {
            const float w = wet.getNext();
            for (int c = 0; c < channels; ++c)
            {
                float* x = band.getWritePointer (c);
                const float d = dry.getSample (c, i);
                x[i] = d + w * (x[i] - d);
            }
        }

        if (mode == ChannelMode::mid || mode == ChannelMode::side)
        {
            st::dsp::decodeMidSide (band.getWritePointer (0), band.getWritePointer (1), n);
            st::dsp::decodeMidSide (dry.getWritePointer (0), dry.getWritePointer (1), n);
        }
    }
    else
    {
        wet.skip (n);
        feedbackGain.skip (n);
    }

    // Bypass crossfade, level/pan, mute/solo.
    float* l = band.getWritePointer (0);
    float* r = channels > 1 ? band.getWritePointer (1) : nullptr;
    const float* dl = dry.getReadPointer (0);
    const float* dr = channels > 1 ? dry.getReadPointer (1) : nullptr;
    float lvl = levelFollower;

    for (int i = 0; i < n; ++i)
    {
        const float bp = bypassMix.getNext();
        const float a = audibleGain.getNext();
        const float gl = gainL.getNext(), gr = gainR.getNext();

        float yl = dl[i] + bp * (gl * l[i] - dl[i]);
        yl *= a;
        l[i] = yl;
        float peak = std::abs (yl);

        if (r != nullptr)
        {
            float yr = dr[i] + bp * (gr * r[i] - dr[i]);
            yr *= a;
            r[i] = yr;
            peak = juce::jmax (peak, std::abs (yr));
        }

        lvl = peak > lvl ? peak : lvl * levelCoeff;
    }

    levelFollower = st::dsp::flushDenormal (lvl);
    outputLevelDb.store (juce::Decibels::gainToDecibels (levelFollower, -100.0f), std::memory_order_relaxed);
}

void BandProcessor::processSpace (juce::AudioBuffer<float>& band, int n, float duckSourcePeak) noexcept
{
    const auto& sp = settings.space;
    const bool wanted = sp.mix > 0.0005f;

    if (! wanted && ! spaceRunning)
    {
        spaceLevelDb.store (-100.0f, std::memory_order_relaxed);
        spaceDuckDb.store (0.0f, std::memory_order_relaxed);
        return;
    }

    if (wanted && ! spaceRunning)
    {
        reverb.reset();
        spaceComp.reset();
        spaceMix.setCurrentAndTarget (0.0f);
    }
    spaceRunning = true;
    spaceMix.setTarget (wanted ? sp.mix : 0.0f);

    const int channels = juce::jmin (2, band.getNumChannels());
    const float* inL = band.getReadPointer (0);
    const float* inR = channels > 1 ? band.getReadPointer (1) : inL;
    float* wl = spaceWet.getWritePointer (0);
    float* wr = spaceWet.getWritePointer (1);
    reverb.process (inL, inR, wl, wr, n);

    if (sp.compression > 0.0005f)
    {
        juce::AudioBuffer<float> view (spaceWet.getArrayOfWritePointers(), 2, n);
        spaceComp.process (view, n);
    }

    // Ducking: follow the chosen source (instant attack, adjustable release) and pull the wet down.
    const float blockSeconds = (float) ((double) n / fs);
    const float rel = std::exp (-blockSeconds / juce::jmax (0.01f, sp.duckReleaseMs * 0.001f));
    duckEnv = duckSourcePeak > duckEnv ? duckSourcePeak : duckEnv * rel;
    duckEnv = st::dsp::flushDenormal (duckEnv);
    const float envDb = juce::Decibels::gainToDecibels (duckEnv, -100.0f);
    const float reductionDb = sp.duck * 30.0f * juce::jlimit (0.0f, 1.0f, (envDb + 48.0f) / 42.0f);
    duckGain.setTarget (juce::Decibels::decibelsToGain (-reductionDb));

    // The wet follows the band's mute / solo / bypass state.
    const float audible = audibleGain.getCurrent() * bypassMix.getCurrent();
    float* outL = band.getWritePointer (0);
    float* outR = channels > 1 ? band.getWritePointer (1) : nullptr;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float gn = spaceMix.getNext() * duckGain.getNext() * audible;
        const float l = wl[i] * gn, r = wr[i] * gn;
        if (outR != nullptr) { outL[i] += l; outR[i] += r; }
        else outL[i] += 0.5f * (l + r);
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
    }

    spaceLevel = juce::jmax (peak, spaceLevel * 0.97f);
    spaceLevelDb.store (juce::Decibels::gainToDecibels (spaceLevel, -100.0f), std::memory_order_relaxed);
    spaceDuckDb.store (-reductionDb, std::memory_order_relaxed);

    if (! wanted && ! spaceMix.isSmoothing() && spaceMix.getCurrent() <= 0.0f)
        spaceRunning = false;
}

//==============================================================================
void Engine::prepare (double sampleRate, int maxBlockSize, int channels)
{
    baseRate = sampleRate;
    maxBlock = maxBlockSize;
    numCh = juce::jlimit (1, 2, channels);

    oversampler.prepare (sampleRate, maxBlockSize, numCh);
    dryDelay.prepare (numCh, oversampler.getLatencySamples (3) + 64);
    dryBuffer.setSize (numCh, maxBlockSize, false, false, true);

    const int maxOs = oversampler.getMaxOversampledBlockSize();
    for (auto& b : bandBuffers)
        b.setSize (numCh, maxOs, false, false, true);
    osInput.setSize (numCh, maxOs, false, false, true);

    for (auto* s : { &inGain, &outGain, &mixSmoother, &panL, &panR })
        s->prepare (sampleRate, 30.0);
    inGain.setCurrentAndTarget (juce::Decibels::decibelsToGain (global.inputGainDb));
    outGain.setCurrentAndTarget (juce::Decibels::decibelsToGain (global.outputGainDb));
    mixSmoother.setCurrentAndTarget (global.mix);
    panL.setCurrentAndTarget (1.0f);
    panR.setCurrentAndTarget (1.0f);

    currentFactor = -1;
    applyQuality();
    reset();
}

void Engine::applyQuality()
{
    const int q = juce::jlimit (0, 3, global.qualityIndex);
    if (q == currentFactor) return;

    currentFactor = q;
    oversampler.setFactorIndex (q);
    dryDelay.setDelay (oversampler.getLatencySamples (q));

    const double osRate = baseRate * (1 << q);
    crossover.prepare (osRate, maxBlock * (1 << q), numCh);
    crossover.setNumBands (global.numBands);
    crossover.setSlope (global.slope);
    for (int i = 0; i < kMaxCrossovers; ++i)
        crossover.setCrossoverFrequency (i, global.crossovers[(size_t) i]);

    for (int b = 0; b < kMaxBands; ++b)
    {
        bands[(size_t) b].prepare (osRate, oversampler.getMaxOversampledBlockSize(), numCh, 1 << q);
        bands[(size_t) b].setSettings (bandSettings[(size_t) b]);
    }
}

void Engine::reset()
{
    oversampler.reset();
    crossover.reset();
    dryDelay.reset();
    for (auto& b : bands) b.reset();
}

void Engine::setGlobal (const GlobalSettings& g) noexcept
{
    global = g;

    crossover.setNumBands (juce::jlimit (1, kMaxBands, g.numBands));
    crossover.setSlope (g.slope);

    // Keep crossovers ascending (the UI does too; this guards automation).
    float last = 10.0f;
    for (int i = 0; i < kMaxCrossovers; ++i)
    {
        const float f = juce::jmax (g.crossovers[(size_t) i], last * 1.12f);
        crossover.setCrossoverFrequency (i, f);
        last = f;
    }

    inGain.setTarget (juce::Decibels::decibelsToGain (g.inputGainDb));
    outGain.setTarget (juce::Decibels::decibelsToGain (g.outputGainDb));
    mixSmoother.setTarget (juce::jlimit (0.0f, 1.0f, g.mix));
    const float p = juce::jlimit (-1.0f, 1.0f, g.outputPan);
    panL.setTarget (p <= 0.0f ? 1.0f : std::cos (p * juce::MathConstants<float>::halfPi));
    panR.setTarget (p >= 0.0f ? 1.0f : std::cos (-p * juce::MathConstants<float>::halfPi));
}

void Engine::setBand (int band, const BandSettings& s) noexcept
{
    bandSettings[(size_t) band] = s;
    bands[(size_t) band].setSettings (s);
}

void Engine::process (juce::AudioBuffer<float>& buffer, int n) noexcept
{
    jassert (n <= maxBlock);
    applyQuality();

    const int channels = juce::jmin (numCh, buffer.getNumChannels());

    // Dry path (latency compensated) for the global mix.
    for (int c = 0; c < channels; ++c)
        dryBuffer.copyFrom (c, 0, buffer, c, 0, n);
    {
        juce::AudioBuffer<float> dryView (dryBuffer.getArrayOfWritePointers(), channels, n);
        dryDelay.process (dryView, n);
    }

    // Input gain
    for (int i = 0; i < n; ++i)
    {
        const float gIn = inGain.getNext();
        for (int c = 0; c < channels; ++c)
            buffer.getWritePointer (c)[i] *= gIn;
    }

    // Up-sample
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) channels, (size_t) n);
    auto up = oversampler.processUp (block);
    const int osN = (int) up.getNumSamples();

    for (int c = 0; c < channels; ++c)
        osInput.copyFrom (c, 0, up.getChannelPointer ((size_t) c), osN);

    juce::AudioBuffer<float> osView (osInput.getArrayOfWritePointers(), channels, osN);
    float inputPeak = 0.0f;
    for (int c = 0; c < channels; ++c)
        inputPeak = juce::jmax (inputPeak, osView.getMagnitude (c, 0, osN));

    crossover.process (osView, bandBuffers.data(), osN);

    // Process bands and sum into the up-sampled block.
    up.clear();
    const int active = juce::jlimit (1, kMaxBands, global.numBands);

    std::array<float, kMaxBands> bandPeak {};
    float allPeak = 0.0f;

    for (int b = 0; b < active; ++b)
    {
        juce::AudioBuffer<float> bandView (bandBuffers[(size_t) b].getArrayOfWritePointers(), channels, osN);
        if (onBandSplit) onBandSplit (b, bandView, osN);
        bands[(size_t) b].process (bandView, osN);

        for (int c = 0; c < channels; ++c)
            bandPeak[(size_t) b] = juce::jmax (bandPeak[(size_t) b], bandView.getMagnitude (c, 0, osN));
        allPeak = juce::jmax (allPeak, bandPeak[(size_t) b]);
    }

    // Space (per-band reverb) + ducking, then sum.
    for (int b = 0; b < active; ++b)
    {
        juce::AudioBuffer<float> bandView (bandBuffers[(size_t) b].getArrayOfWritePointers(), channels, osN);
        const int src = bands[(size_t) b].getSettings().space.duckSource;
        float duckLevel = bandPeak[(size_t) b];
        if (src == 1) duckLevel = allPeak;
        else if (src == 2) duckLevel = inputPeak;
        else if (src >= 3) duckLevel = (src - 3) < active ? bandPeak[(size_t) (src - 3)] : 0.0f;

        bands[(size_t) b].processSpace (bandView, osN, duckLevel);

        for (int c = 0; c < channels; ++c)
            juce::FloatVectorOperations::add (up.getChannelPointer ((size_t) c), bandView.getReadPointer (c), osN);
    }

    // Down-sample back into `buffer`
    oversampler.processDown (block);

    // Global mix, output gain & pan
    for (int i = 0; i < n; ++i)
    {
        const float m = mixSmoother.getNext();
        const float g = outGain.getNext();
        const float pl = panL.getNext(), pr = panR.getNext();

        for (int c = 0; c < channels; ++c)
        {
            float* x = buffer.getWritePointer (c);
            const float d = dryBuffer.getSample (c, i);
            const float pg = channels == 2 ? (c == 0 ? pl : pr) : 1.0f;
            x[i] = (d + m * (x[i] - d)) * g * pg;
        }
    }

    st::dsp::isFiniteBuffer (buffer, n, true);
}

} // namespace stsat
