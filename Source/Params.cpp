#include "Params.h"
#include "dsp/Saturation.h"

namespace stsat
{

using namespace st::params;

int getDefaultStyleIndex()
{
    const auto names = getStyleNames();
    for (auto candidate : { "Warm Valve", "Warm Tube", "Gentle" })
    {
        const int i = names.indexOf (candidate);
        if (i >= 0) return i;
    }
    return 0;
}

juce::StringArray getDuckSourceNames()
{
    juce::StringArray s { "This Band", "All Bands", "Input" };
    for (int b = 0; b < kMaxBands; ++b)
        s.add ("Band " + juce::String (b + 1));
    return s;
}

st::mod::Config getModConfig()
{
    st::mod::Config c;
    c.numLfos = 4;
    c.numEnvelopes = 2;
    c.numMacros = 4;
    c.maxRoutings = 48;
    c.envelopeInputs = { "Input" };
    for (int b = 0; b < kMaxBands; ++b)
        c.envelopeInputs.add ("Band " + juce::String (b + 1));
    return c;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const st::mod::Config& modConfig)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ------------------------------------------------------------------ global
    {
        auto global = std::make_unique<juce::AudioProcessorParameterGroup> ("global", "Global", " | ");
        global->addChild (makeFloat (ids::inputGain, "Input Gain", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        global->addChild (makeFloat (ids::outputGain, "Output Gain", linear (-24.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        global->addChild (makeFloat (ids::mix, "Mix", linear (0.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
        global->addChild (makeFloat (ids::outputPan, "Output Pan", linear (-100.0f, 100.0f, 0.01f), 0.0f, percentAttributes (true)));
        global->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids::numBands, 1 }, "Bands", 1, kMaxBands, 1));
        global->addChild (makeChoice (ids::slope, "Crossover Slope", { "12 dB/oct", "24 dB/oct", "48 dB/oct" }, 1));
        global->addChild (makeChoice (ids::quality, "Quality", { "Eco 1x", "Good 2x", "High 4x", "Ultra 8x" }, 1));
        global->addChild (makeBool (ids::autoGain, "Auto Gain", true));

        for (int i = 0; i < kMaxCrossovers; ++i)
            global->addChild (makeFloat (ids::crossover (i), "Crossover " + juce::String (i + 1),
                                         logarithmic (20.0f, 20000.0f), defaults::crossovers[i], hzAttributes()));

        layout.add (std::move (global));
    }

    // ------------------------------------------------------------------- bands
    const auto styles = getStyleNames();
    const int defStyle = getDefaultStyleIndex();

    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto n = "Band " + juce::String (b + 1) + " ";
        auto id = [b] (const char* s) { return ids::band (b, s); };
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("band" + juce::String (b + 1), "Band " + juce::String (b + 1), " | ");

        group->addChild (makeChoice (id ("style"), n + "Style", styles, defStyle));
        group->addChild (makeFloat (id ("drive"), n + "Drive", skewed (0.0f, 48.0f, 12.0f, 0.01f), defaults::drive, dbAttributes()));
        group->addChild (makeFloat (id ("drive_spread"), n + "Stereo Drive Spread", linear (-24.0f, 24.0f, 0.01f), 0.0f,
                                    juce::AudioParameterFloatAttributes()
                                        .withStringFromValueFunction ([] (float v, int)
                                        {
                                            if (std::abs (v) < 0.05f) return juce::String ("Centre");
                                            return juce::String (std::abs (v), 1) + " dB " + (v < 0.0f ? "L" : "R");
                                        })
                                        .withValueFromStringFunction ([] (const juce::String& s) { return s.retainCharacters ("-+.0123456789").getFloatValue(); })));
        group->addChild (makeFloat (id ("dyn"), n + "Dynamics", linear (-100.0f, 100.0f, 0.01f), 0.0f, percentAttributes (true)));
        group->addChild (makeFloat (id ("dyn_up_thresh"), n + "Upward Threshold", linear (-72.0f, 0.0f, 0.01f), -36.0f, dbAttributes()));
        group->addChild (makeFloat (id ("dyn_up_ratio"), n + "Upward Ratio", skewed (1.0f, 20.0f, 3.0f, 0.01f), 3.0f, ratioAttributes()));
        group->addChild (makeFloat (id ("dyn_down_thresh"), n + "Downward Threshold", linear (-60.0f, 0.0f, 0.01f), -18.0f, dbAttributes()));
        group->addChild (makeFloat (id ("dyn_down_ratio"), n + "Downward Ratio", skewed (1.0f, 100.0f, 4.0f, 0.01f), 4.0f, ratioAttributes()));
        group->addChild (makeFloat (id ("dyn_attack"), n + "Attack", skewed (0.1f, 300.0f, 15.0f, 0.01f), 8.0f, msAttributes()));
        group->addChild (makeFloat (id ("dyn_release"), n + "Release", skewed (5.0f, 3000.0f, 200.0f, 0.1f), 120.0f, msAttributes()));
        group->addChild (makeChoice (id ("dyn_pos"), n + "Dynamics Position", { "Pre", "Post" }, 1));
        group->addChild (makeFloat (id ("tone_low"),  n + "Tone Low",      linear (-12.0f, 12.0f, 0.01f), 0.0f, dbAttributes()));
        group->addChild (makeFloat (id ("tone_lmid"), n + "Tone Low-Mid",  linear (-12.0f, 12.0f, 0.01f), 0.0f, dbAttributes()));
        group->addChild (makeFloat (id ("tone_hmid"), n + "Tone High-Mid", linear (-12.0f, 12.0f, 0.01f), 0.0f, dbAttributes()));
        group->addChild (makeFloat (id ("tone_high"), n + "Tone High",     linear (-12.0f, 12.0f, 0.01f), 0.0f, dbAttributes()));
        group->addChild (makeFloat (id ("fb"), n + "Feedback", linear (0.0f, 100.0f, 0.01f), 0.0f, percentAttributes()));
        group->addChild (makeFloat (id ("fb_freq"), n + "Feedback Frequency", logarithmic (20.0f, 20000.0f), 1000.0f, hzAttributes()));
        group->addChild (makeFloat (id ("mix"), n + "Mix", linear (0.0f, 100.0f, 0.01f), 100.0f, percentAttributes()));
        group->addChild (makeFloat (id ("level"), n + "Level", linear (-36.0f, 24.0f, 0.01f), 0.0f, dbAttributes()));
        group->addChild (makeFloat (id ("pan"), n + "Pan", linear (-100.0f, 100.0f, 0.01f), 0.0f, percentAttributes (true)));
        group->addChild (makeChoice (id ("chan"), n + "Channels", { "Stereo", "Mid", "Side", "Left", "Right" }, 0));

        // Space: per-band reverb with compression and ducking
        group->addChild (makeChoice (id ("space_type"), n + "Space Type", spaceTypeNames, 3));
        group->addChild (makeFloat (id ("space_mix"), n + "Space Amount", linear (0.0f, 100.0f, 0.01f), 0.0f, percentAttributes()));
        group->addChild (makeFloat (id ("space_decay"), n + "Space Decay", skewed (0.1f, 20.0f, 2.0f, 0.01f), 2.0f,
                                    juce::AudioParameterFloatAttributes()
                                        .withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; })
                                        .withValueFromStringFunction ([] (const juce::String& s) { auto t = s.toLowerCase(); float v = t.retainCharacters (".0123456789").getFloatValue(); return t.contains ("ms") ? v * 0.001f : v; })));
        group->addChild (makeFloat (id ("space_size"), n + "Space Size", linear (0.0f, 100.0f, 0.01f), 50.0f, percentAttributes()));
        group->addChild (makeFloat (id ("space_predelay"), n + "Space Pre-delay", skewed (0.0f, 250.0f, 40.0f, 0.1f), 12.0f, msAttributes()));
        group->addChild (makeFloat (id ("space_tone"), n + "Space Tone", linear (-100.0f, 100.0f, 0.01f), 0.0f, percentAttributes (true)));
        group->addChild (makeFloat (id ("space_width"), n + "Space Width", linear (0.0f, 150.0f, 0.01f), 100.0f, percentAttributes()));
        group->addChild (makeFloat (id ("space_comp"), n + "Space Compression", linear (0.0f, 100.0f, 0.01f), 0.0f, percentAttributes()));
        group->addChild (makeFloat (id ("space_duck"), n + "Space Ducking", linear (0.0f, 100.0f, 0.01f), 0.0f, percentAttributes()));
        group->addChild (makeChoice (id ("space_duck_src"), n + "Space Duck Source", getDuckSourceNames(), 0));
        group->addChild (makeFloat (id ("space_duck_release"), n + "Space Duck Release", skewed (20.0f, 2000.0f, 250.0f, 0.1f), 250.0f, msAttributes()));
        group->addChild (makeBool (id ("mute"), n + "Mute", false));
        group->addChild (makeBool (id ("solo"), n + "Solo", false));
        group->addChild (makeBool (id ("bypass"), n + "Bypass", false));

        layout.add (std::move (group));
    }

    // -------------------------------------------------------------- modulation
    st::mod::ModMatrix::addParameters (layout, modConfig);

    return layout;
}

juce::StringArray getModulatableParameterIds()
{
    juce::StringArray out { ids::inputGain, ids::outputGain, ids::mix, ids::outputPan };
    for (int i = 0; i < kMaxCrossovers; ++i)
        out.add (ids::crossover (i));

    for (int b = 0; b < kMaxBands; ++b)
        for (auto s : { "drive", "drive_spread", "dyn", "dyn_up_thresh", "dyn_up_ratio", "dyn_down_thresh", "dyn_down_ratio",
                        "dyn_attack", "dyn_release", "tone_low", "tone_lmid", "tone_hmid", "tone_high", "fb", "fb_freq", "mix", "level", "pan",
                        "space_mix", "space_decay", "space_size", "space_predelay", "space_tone", "space_width", "space_comp", "space_duck" })
            out.add (ids::band (b, s));

    return out;
}

} // namespace stsat
