#pragma once
#include <st_shell/st_shell.h>

namespace stsat
{

constexpr int kMaxBands = 6;
constexpr int kMaxCrossovers = kMaxBands - 1;

enum class ChannelMode { stereo, mid, side, left, right };
enum class DynamicsPosition { pre, post };

namespace ids
{
    inline const juce::String inputGain  = "in_gain";
    inline const juce::String outputGain = "out_gain";
    inline const juce::String mix        = "mix";
    inline const juce::String numBands   = "num_bands";
    inline const juce::String slope      = "xover_slope";
    inline const juce::String quality    = "quality";
    inline const juce::String autoGain   = "auto_gain";
    inline const juce::String outputPan  = "out_pan";

    inline juce::String crossover (int i) { return "xover_" + juce::String (i + 1); }   // 0..4

    // Per-band parameter suffixes. Full ID: "b<n>_<suffix>", n = 1..6
    inline const juce::StringArray bandSuffixes {
        "style", "drive", "dyn", "dyn_up_thresh", "dyn_up_ratio", "dyn_down_thresh", "dyn_down_ratio",
        "dyn_attack", "dyn_release", "dyn_pos", "tone_low", "tone_lmid", "tone_hmid", "tone_high", "fb", "fb_freq", "mix", "level", "pan",
        "chan", "mute", "solo", "bypass"
    };

    inline juce::String band (int b, const juce::String& suffix) { return "b" + juce::String (b + 1) + "_" + suffix; }
}

/** Defaults used by the layout, band insertion and factory presets. */
namespace defaults
{
    constexpr float crossovers[kMaxCrossovers] = { 120.0f, 700.0f, 2500.0f, 6000.0f, 12000.0f };
    constexpr float drive = 6.0f;
    // Tone EQ bands: frequency (Hz) and Q
    constexpr float toneFreq[4] = { 110.0f, 550.0f, 2600.0f, 7500.0f };
    constexpr float toneQ[4]    = { 0.71f, 0.9f, 0.9f, 0.71f };
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const st::mod::Config&);

/** IDs of every parameter that can be a modulation target. */
juce::StringArray getModulatableParameterIds();

st::mod::Config getModConfig();

/** Default style index (by name, falls back to 0). */
int getDefaultStyleIndex();

} // namespace stsat
