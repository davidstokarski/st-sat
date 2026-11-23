// Use-case videos of the real editor (website), on the shell's video kit (st_shell/testing/st_video.h): a
// synthesized mix plays through the processor while a scripted pointer works the UI.
//   StSatPluginTests --video <dir> [scene ...]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <st_shell/testing/st_video.h>

using st::video::Director;
using st::video::Music;
using st::video::Scene;

namespace
{
/** ST Sat places: the band display (crossovers, band bodies) and the floating band panel's controls. */
struct Sat
{
    Director& d;
    StSatProcessor& proc() const { return dynamic_cast<StSatProcessor&> (d.processor); }
    stsat::MultibandDisplay& display() const { return *d.findType<stsat::MultibandDisplay>(); }

    /** A point in the band area at `hz` (log axis over the display's band range), `y` px below the display's top. */
    juce::Point<float> body (float hz, float y = 58.0f) const
    {
        auto& disp = display();
        juce::Range<float> all;
        for (int b = 0; b < proc().getNumBands(); ++b)
            all = b == 0 ? disp.getBandXRange (b) : all.getUnionWith (disp.getBandXRange (b));
        const float t = std::log (hz / 20.0f) / std::log (1000.0f);
        return d.pointIn (disp, { all.getStart() + all.getLength() * t, y });
    }
    /** The crossover above `band`. */
    juce::Point<float> crossover (int band, float y = 58.0f) const { return d.pointIn (display(), { display().getBandXRange (band).getEnd(), y }); }
    juce::Point<float> knob (int band, const char* suffix) const { return d.controlCentre (stsat::ids::band (band, suffix)); }
};

std::vector<Scene> scenes()
{
    std::vector<Scene> v;

    // 1. A drum bus saturated in bands: split into lows / mids / highs, drive the lows, a little bite on top
    v.push_back ({ "drum-bus", 15.0, [] (juce::AudioProcessor&, Music& m)
    {
        m.drums = 1.0f; m.rooms = 0.6f; m.bass = 0.8f;
    }, [] (Director& dir)
    {
        auto s = std::make_shared<Sat> (Sat { dir });
        dir.wait (1.2);
        dir.move ([s] { return s->body (170.0f); }, 0.9);
        dir.doubleClick();                                   // split: lows | the rest
        dir.wait (0.4);
        dir.move ([s] { return s->body (2600.0f); }, 0.9);
        dir.doubleClick();                                   // split again: mids | highs
        dir.wait (0.4);
        dir.move ([s] { return s->body (60.0f); }, 0.9);
        dir.click();                                         // select the lows
        dir.wait (0.3);
        dir.move ([s] { return s->knob (0, "drive"); }, 0.9);
        dir.dragBy ({ 0.0f, -70.0f }, 1.1);                  // more drive on the kick and bass
        dir.move ([s] { return s->body (9000.0f); }, 1.0);
        dir.click();                                         // select the highs
        dir.move ([s] { return s->knob (2, "drive"); }, 0.9);
        dir.dragBy ({ 0.0f, -40.0f }, 0.9);
        dir.move ([s] { return s->crossover (1); }, 0.9);
        dir.drag ([s] { return s->body (4200.0f); }, 0.9); // move the mids / highs split up
        dir.wait (1.6);
    } });

    // 2. Space on a vocal: a little warmth, then reverb from the band's Space, tamed by ducking
    v.push_back ({ "vocal-space", 15.0, [] (juce::AudioProcessor& p, Music& m)
    {
        st::video::setParam (p, stsat::ids::band (0, "drive"), 6.0f);
        m.vocal = 1.0f; m.sib = 0.4f; m.pad = 0.35f; m.bass = 0.3f;
    }, [] (Director& dir)
    {
        auto s = std::make_shared<Sat> (Sat { dir });
        dir.wait (1.3);
        dir.move ([s] { return s->knob (0, "space_mix"); }, 1.0);
        dir.dragBy ({ 0.0f, -80.0f }, 1.2);                  // Space amount up
        dir.wait (0.6);
        dir.move ([&dir] { return dir.button ("Space: reverb sound, compression and ducking"); }, 0.8);
        dir.click();                                         // open the Space detail
        dir.wait (0.6);
        dir.move ([s] { return s->knob (0, "space_decay"); }, 0.9);
        dir.dragBy ({ 0.0f, -45.0f }, 1.0);                  // longer tail
        dir.move ([s] { return s->knob (0, "space_duck"); }, 0.9);
        dir.dragBy ({ 0.0f, -70.0f }, 1.1);                  // duck the tail under the voice
        dir.wait (3.0);
    } });
    return v;
}
}

int runVideoRender (int argc, char** argv);
int runVideoRender (int argc, char** argv)
{
    return st::video::run (argc, argv, "st-sat", [] { return std::make_unique<StSatProcessor>(); }, scenes());
}
