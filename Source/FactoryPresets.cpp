#include "FactoryPresets.h"
#include "Params.h"
#include "dsp/Saturation.h"

namespace stsat
{

namespace
{
    using Values = std::vector<std::pair<juce::String, float>>;

    float style (const char* name)
    {
        const int i = getStyleNames().indexOf (name);
        jassert (i >= 0);   // style renamed? update the preset
        return (float) juce::jmax (0, i);
    }

    /** Fluent helper for readable preset definitions. */
    struct P
    {
        Values v;

        P& bands (int n, std::initializer_list<float> xovers = {})
        {
            v.push_back ({ ids::numBands, (float) n });
            int i = 0;
            for (auto f : xovers) v.push_back ({ ids::crossover (i++), f });
            return *this;
        }

        P& set (const juce::String& id, float value) { v.push_back ({ id, value }); return *this; }
        P& band (int b, const char* suffix, float value) { return set (ids::band (b, suffix), value); }

        P& sat (int b, const char* styleName, float drive, float dyn = 0.0f, float mix = 100.0f)
        {
            band (b, "style", style (styleName));
            band (b, "drive", drive);
            band (b, "dyn", dyn);
            return band (b, "mix", mix);
        }

        /** Old-style tilt (-100..100) mapped onto the low/high shelves of the tone EQ. */
        P& tilt (int b, float amount)
        {
            band (b, "tone_low", -amount * 0.06f);
            return band (b, "tone_high", amount * 0.08f);
        }

        /** OTT-style up/down settings for a band. */
        P& dynamics (int b, float upT, float upR, float downT, float downR, float att, float rel)
        {
            band (b, "dyn_up_thresh", upT);
            band (b, "dyn_up_ratio", upR);
            band (b, "dyn_down_thresh", downT);
            band (b, "dyn_down_ratio", downR);
            band (b, "dyn_attack", att);
            return band (b, "dyn_release", rel);
        }
    };
}

void registerFactoryPresets (st::PresetManager& pm)
{
    auto add = [&pm] (const char* name, const char* category, const P& p) { pm.addFactoryPreset (name, category, p.v); };

    // ---------------------------------------------------------------- Mix Bus
    add ("Console Glue", "Mix Bus",
         P().bands (1).sat (0, "Console Bus", 7.0f, 18.0f).dynamics (0, -40.0f, 1.6f, -14.0f, 2.0f, 20.0f, 180.0f));

    add ("Tape Bus", "Mix Bus",
         P().bands (1).sat (0, "Soft Tape", 9.0f).tilt (0, -8.0f));

    add ("Analog Sheen", "Mix Bus",
         P().bands (2, { 3500.0f })
            .sat (0, "Iron Transformer", 5.0f)
            .sat (1, "Clean Triode", 9.0f).tilt (1, 18.0f).band (1, "mix", 60.0f));

    // ------------------------------------------------------------------ Drums
    add ("Drum Smash", "Drums",
         P().bands (3, { 140.0f, 3200.0f })
            .sat (0, "Dense", 10.0f, 50.0f).dynamics (0, -38.0f, 3.0f, -16.0f, 4.0f, 12.0f, 120.0f)
            .sat (1, "Crunch Stack", 16.0f, 40.0f).band (1, "mix", 70.0f)
            .sat (2, "Hot Tape", 10.0f, 30.0f));

    add ("Room Crusher", "Drums",
         P().bands (1).sat (0, "Hard Clip", 24.0f, 85.0f, 55.0f)
            .dynamics (0, -50.0f, 6.0f, -20.0f, 8.0f, 2.0f, 90.0f));

    add ("Snare Crack", "Drums",
         P().bands (3, { 180.0f, 1800.0f })
            .sat (0, "Clean Triode", 2.0f)
            .sat (1, "Pushed Valve", 14.0f, -30.0f)
            .sat (2, "Silicon", 12.0f).band (2, "mix", 65.0f));

    // ------------------------------------------------------------------- Bass
    add ("Bass Growl", "Bass",
         P().bands (2, { 160.0f })
            .sat (0, "Clean Triode", 3.0f)
            .sat (1, "Bass Rig", 20.0f, 30.0f).band (1, "level", -2.0f));

    add ("Fuzz Above the Sub", "Bass",
         P().bands (2, { 120.0f })
            .band (0, "bypass", 1.0f)
            .sat (1, "Fuzz Pedal", 22.0f, 0.0f, 55.0f));

    // ----------------------------------------------------------------- Vocals
    add ("Vocal Presence", "Vocals",
         P().bands (3, { 250.0f, 4500.0f })
            .sat (0, "Soft Tape", 2.0f)
            .sat (1, "Warm Valve", 8.0f, 30.0f).dynamics (1, -42.0f, 2.0f, -18.0f, 3.0f, 10.0f, 140.0f)
            .sat (2, "Gentle Glow", 7.0f).tilt (2, 20.0f));

    add ("Radio Voice", "Vocals",
         P().bands (1).sat (0, "Glass Radio", 18.0f).tilt (0, 25.0f));

    // -------------------------------------------------------------- Dynamics
    add ("Up & Down Multiband", "Dynamics",
         P().bands (3, { 120.0f, 2500.0f })
            .sat (0, "Gentle Glow", 2.0f, 100.0f).dynamics (0, -42.0f, 4.0f, -20.0f, 6.0f, 15.0f, 160.0f)
            .sat (1, "Gentle Glow", 2.0f, 100.0f).dynamics (1, -42.0f, 4.0f, -20.0f, 6.0f, 8.0f, 120.0f)
            .sat (2, "Gentle Glow", 2.0f, 100.0f).dynamics (2, -42.0f, 4.0f, -20.0f, 6.0f, 3.0f, 80.0f)
            .set (ids::mix, 60.0f));

    add ("Upward Detail", "Dynamics",
         P().bands (2, { 3000.0f })
            .sat (0, "Clean Triode", 0.0f, 0.0f)
            .sat (1, "Soft Tape", 4.0f, 70.0f).dynamics (1, -30.0f, 6.0f, 0.0f, 1.0f, 5.0f, 100.0f));

    add ("Transient Expander", "Dynamics",
         P().bands (1).sat (0, "Clean Triode", 3.0f, -60.0f).dynamics (0, -50.0f, 2.0f, -24.0f, 3.0f, 1.0f, 80.0f));

    // --------------------------------------------------------------- Creative
    add ("Resonant Feedback", "Creative",
         P().bands (1).sat (0, "Pushed Valve", 14.0f).band (0, "fb", 65.0f).band (0, "fb_freq", 800.0f).band (0, "mix", 70.0f));

    add ("Side Tape Width", "Creative",
         P().bands (2, { 400.0f })
            .band (0, "bypass", 1.0f)
            .sat (1, "Soft Tape", 12.0f).band (1, "chan", 2.0f).band (1, "level", 2.0f));

    add ("Worn Cassette", "Creative",
         P().bands (1).sat (0, "Worn Cassette", 14.0f).tilt (0, -40.0f));

    add ("Lead Amp Highs", "Creative",
         P().bands (2, { 900.0f })
            .sat (0, "Warm Valve", 4.0f)
            .sat (1, "Lead Stack", 26.0f, 20.0f).band (1, "mix", 50.0f));
}

} // namespace stsat
