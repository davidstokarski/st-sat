// Whole-plugin smoke / stress tests and editor snapshots.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Strip.h"

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

void setParam (juce::AudioProcessor& p, const juce::String& id, float plain)
{
    auto* sp = dynamic_cast<StSatProcessor*> (&p);
    auto& prm = sp->param (id);
    prm.setValueNotifyingHost (prm.convertTo0to1 (plain));
}

float runNoise (StSatProcessor& p, int blocks, int blockSize, bool& finite, double sr = 48000.0)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::Random rng (1);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
                buf.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.25f
                                     + 0.3f * std::sin ((float) (b * blockSize + i) * 0.05f));
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, std::abs (v));
            }
    }
    juce::ignoreUnused (sr);
    return peak;
}

/** Renders the editor after letting its timers run for a while (needs a running message loop). */
struct Snapshotter : juce::Timer
{
    struct Job { std::function<void()> setup; juce::String name; float scale = 1.0f;
                 std::function<void (juce::Component&)> afterOpen; };
    StSatProcessor& p;
    juce::File dir;
    std::vector<Job> jobs;
    size_t index = 0;
    int ticks = 0;
    bool opened = false;
    std::unique_ptr<juce::AudioProcessorEditor> ed;

    Snapshotter (StSatProcessor& proc, juce::File d) : p (proc), dir (d) {}

    void timerCallback() override
    {
        if (ed == nullptr)
        {
            if (index >= jobs.size()) { stopTimer(); juce::JUCEApplicationBase::quit(); return; }
            if (jobs[index].setup) jobs[index].setup();
            ed.reset (p.createEditor());
            ed->setSize (juce::roundToInt ((float) StSatEditor::designWidth * jobs[index].scale),
                         juce::roundToInt ((float) StSatEditor::designHeight * jobs[index].scale));
            ticks = 0;
            opened = false;
        }

        bool f = true;
        runNoise (p, 4, 256, f);

        if (! opened && ticks > 6 && jobs[index].afterOpen) { jobs[index].afterOpen (*ed); opened = true; }

        if (++ticks > 50)
        {
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            auto file = dir.getChildFile (jobs[index].name + ".png");
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (img, out);
            std::cout << "wrote " << file.getFullPathName() << "\n";
            ed.reset();
            ++index;
        }
    }
};
}

static int runTests()
{
    juce::ScopedJuceInitialiser_GUI gui;

    auto proc = std::make_unique<StSatProcessor>();
    auto& p = *proc;
    p.setPlayConfigDetails (2, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);


    // ---- Stress: every style, every band count, every quality, random params
    bool finite = true;
    const int numStyles = stsat::getNumStyles();
    for (int q = 0; q < 4; ++q)
    {
        setParam (p, stsat::ids::quality, (float) q);
        p.applyQualityChange();   // normally async on the message thread
        for (int nb = 1; nb <= stsat::kMaxBands; ++nb)
        {
            setParam (p, stsat::ids::numBands, (float) nb);
            for (int b = 0; b < stsat::kMaxBands; ++b)
            {
                setParam (p, stsat::ids::band (b, "style"), (float) ((b * 5 + nb * 3 + q) % numStyles));
                setParam (p, stsat::ids::band (b, "drive"), (float) (8 * (b + q) % 48));
                setParam (p, stsat::ids::band (b, "dyn"), (b % 2 ? 80.0f : -50.0f));
                setParam (p, stsat::ids::band (b, "fb"), 70.0f);
                setParam (p, stsat::ids::band (b, "chan"), (float) (b % 5));
                setParam (p, stsat::ids::band (b, "space_mix"), 40.0f);
                setParam (p, stsat::ids::band (b, "space_type"), (float) ((b + q) % 8));
                setParam (p, stsat::ids::band (b, "space_duck"), 50.0f);
                setParam (p, stsat::ids::band (b, "space_duck_src"), (float) (b % 9));
                setParam (p, stsat::ids::band (b, "space_comp"), 40.0f);
            }
            const float peak = runNoise (p, 20, 512, finite);
            check (peak < 16.0f, "peak bounded q=" + juce::String (q) + " nb=" + juce::String (nb) + " peak=" + juce::String (peak));
        }
        check (p.getLatencySamples() == p.getEngine().getLatencySamples(), "latency reported q=" + juce::String (q));
    }
    check (finite, "all output finite");

    // ---- Odd block sizes
    for (int bs : { 1, 7, 63, 64, 65, 333, 2048 })
    {
        p.prepareToPlay (44100.0, bs);
        const float peak = runNoise (p, 8, bs, finite);
        check (finite && peak < 16.0f, "block size " + juce::String (bs));
    }

    // ---- Band add / remove keeps things consistent
    p.presets.loadPreset (0);
    p.addBand (500.0f);
    p.addBand (4000.0f);
    check (p.getNumBands() == 3, "addBand x2 -> 3 bands");
    p.modMatrix.addRouting (st::mod::SourceId::lfo (1), stsat::ids::band (2, "drive"), 0.2f);
    p.removeBand (1);
    check (p.getNumBands() == 2, "removeBand -> 2 bands");
    check (p.modMatrix.getRouting (st::mod::SourceId::lfo (1), stsat::ids::band (1, "drive")).has_value(), "routing follows band on remove");
    p.addBand (100.0f);
    check (p.modMatrix.getRouting (st::mod::SourceId::lfo (1), stsat::ids::band (2, "drive")).has_value(), "routing follows band on add");
    p.modMatrix.clearRoutings();

    // ---- State round trip
    juce::MemoryBlock state;
    setParam (p, stsat::ids::band (0, "drive"), 33.0f);
    p.modMatrix.addRouting (st::mod::SourceId::macro (0), stsat::ids::band (0, "drive"), 0.5f);
    p.getStateInformation (state);
    StSatProcessor p2;
    p2.setStateInformation (state.getData(), (int) state.getSize());
    check (std::abs (p2.apvts.getRawParameterValue (stsat::ids::band (0, "drive"))->load() - 33.0f) < 0.05f, "state round trip drive");
    check (p2.modMatrix.getNumRoutings() == 1, "state round trip routing");

    // ---- CPU estimate: 6 bands, 4x, heavy styles
    {
        p.prepareToPlay (48000.0, 512);
        setParam (p, stsat::ids::quality, 2.0f);
        p.applyQualityChange();
        setParam (p, stsat::ids::numBands, 6.0f);
        for (int b = 0; b < 6; ++b) setParam (p, stsat::ids::band (b, "dyn"), 50.0f);
        const int blocks = 400;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runNoise (p, blocks, 512, finite);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double audioMs = blocks * 512 / 48.0;
        std::cout << "CPU (6 bands, 4x, dynamics on): " << juce::String (100.0 * ms / audioMs, 1) << "% of one core\n";
    }

    // ---- Multiview: ST Sat's own module, editable; a second instance on the same track joins the row
    {
        juce::AudioProcessor::TrackProperties tp;
        tp.name = juce::String ("Drums");
        p.updateTrackProperties (tp);
        auto second = std::make_unique<StSatProcessor>();
        second->updateTrackProperties (tp);
        st::InstanceRegistry::get().flush();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (StSatEditor::designWidth, StSatEditor::designHeight);
        auto* e = dynamic_cast<StSatEditor*> (ed.get());
        e->openMultiView();
        auto* mv = e->getMultiView();
        mv->refresh();
        const int n = st::InstanceRegistry::get().getNumInstances();   // other instances may be alive in this test
        check (mv != nullptr && mv->getNumModules() == n && mv->getNumRows() == n - 1,
               "multiview: both Drums instances share one row (" + juce::String (mv->getNumRows()) + " rows, " + juce::String (mv->getNumModules()) + " modules)");
        auto* strip = dynamic_cast<stsat::SatStrip*> (mv->getModuleComponent (second->getInstanceUid()));
        check (strip != nullptr && strip->getNumKnobs() == second->getNumBands() + 2, "ST Sat module: a drive per band + mix + out");
        if (strip != nullptr)
        {
            auto* k = strip->getKnob (0);
            const float before = second->bandParam (0, "drive").getValue();
            auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto c = k->getLocalBounds().getCentre().toFloat();
            auto ev = [&] (juce::Point<float> pos, juce::ModifierKeys m, bool dragged)
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent (src, pos, m, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, k, k, now, c, now, 1, dragged);
            };
            const juce::ModifierKeys lb (juce::ModifierKeys::leftButtonModifier);
            k->mouseDown (ev (c, lb, false));
            for (int i = 1; i <= 10; ++i) k->mouseDrag (ev (c.translated (0.0f, -6.0f * (float) i), lb, true));
            k->mouseUp (ev (c.translated (0.0f, -60.0f), {}, true));
            check (second->bandParam (0, "drive").getValue() > before + 0.1f && std::abs (p.bandParam (0, "drive").getValue() - before) < 0.5f,
                   "editing the module edits that instance");
        }
        juce::Image img (juce::Image::ARGB, mv->getWidth(), mv->getHeight(), true);
        { juce::Graphics g (img); mv->paintEntireComponent (g, true); }
        second.reset();
        check (mv->getNumModules() == n - 1, "removed instance leaves the multiview");
        { juce::Graphics g (img); mv->paintEntireComponent (g, true); }
        ed.reset();
        p.updateTrackProperties ({});
    }

    std::cout << (failures == 0 ? "ALL PASSED" : juce::String (failures) + " FAILURES") << "\n";
    return failures == 0 ? 0 : 1;
}

//==============================================================================
static juce::File snapshotDir;

struct SnapshotApp : juce::JUCEApplicationBase
{
    std::unique_ptr<StSatProcessor> proc;
    std::unique_ptr<Snapshotter> snap;

    const juce::String getApplicationName() override { return "StSatSnapshots"; }
    const juce::String getApplicationVersion() override { return "1"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted (const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception*, const juce::String&, int) override {}
    void shutdown() override { snap.reset(); proc.reset(); }

    void initialise (const juce::String&) override
    {
        proc = std::make_unique<StSatProcessor>();
        auto& p = *proc;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        snapshotDir.createDirectory();

        auto load = [&p] (const juce::String& name)
        {
            for (int i = 0; i < (int) p.presets.getPresets().size(); ++i)
                if (p.presets.getPresets()[(size_t) i].name == name) p.presets.loadPreset (i);
        };

        snap = std::make_unique<Snapshotter> (p, snapshotDir);
        snap->jobs.push_back ({ [&p] { p.presets.loadPreset (0); }, "01_init" });
        snap->jobs.push_back ({ [&p, load] { load ("Drum Smash"); p.uiState.setProperty ("selectedBand", 1, nullptr); }, "02_drum_smash" });
        snap->jobs.push_back ({ [&p, load]
        {
            load ("Up & Down Multiband");
            p.modMatrix.addRouting (st::mod::SourceId::lfo (0), stsat::ids::band (0, "drive"), 0.3f);
            p.modMatrix.addRouting (st::mod::SourceId::lfo (0), stsat::ids::band (0, "tone_high"), -0.4f);
            p.uiState.setProperty ("selectedBand", 0, nullptr);
            p.uiState.setProperty ("dynExpanded", true, nullptr);
        }, "03_updown_mod" });
        snap->jobs.push_back ({ [&p] { p.uiState.setProperty ("dynExpanded", false, nullptr); }, "04_small_scale", 0.75f });

        auto find = [] (juce::Component& root, auto pred) -> juce::Component*
        {
            std::function<juce::Component* (juce::Component&)> rec = [&] (juce::Component& c) -> juce::Component*
            {
                if (pred (c)) return &c;
                for (auto* ch : c.getChildren()) if (auto* r = rec (*ch)) return r;
                return nullptr;
            };
            return rec (root);
        };

        snap->jobs.push_back ({ [&p] { p.presets.loadPreset (0); }, "05_dynamics_popover", 1.0f, [find] (juce::Component& ed)
        {
            if (auto* panel = dynamic_cast<stsat::FloatingBandPanel*> (find (ed, [] (juce::Component& c) { return dynamic_cast<stsat::FloatingBandPanel*> (&c) != nullptr; })))
                if (panel->onDynamicsDetail) panel->onDynamicsDetail();
        } });

        snap->jobs.push_back ({ [&p]
        {
            p.presets.loadPreset (0);
            auto set = [&p] (const char* id, float v) { auto& prm = p.bandParam (0, id); prm.setValueNotifyingHost (prm.convertTo0to1 (v)); };
            set ("space_mix", 35.0f);
            set ("space_type", 2.0f);
            set ("space_decay", 3.2f);
            set ("space_duck", 60.0f);
            set ("space_comp", 30.0f);
        }, "07_space_popover", 1.0f, [find] (juce::Component& ed)
        {
            if (auto* panel = dynamic_cast<stsat::FloatingBandPanel*> (find (ed, [] (juce::Component& c) { return dynamic_cast<stsat::FloatingBandPanel*> (&c) != nullptr; })))
                if (panel->onSpaceDetail) panel->onSpaceDetail();
        } });

        snap->jobs.push_back ({ [&p]
        {
            auto& s = p.param (p.modMatrix.getParamId (st::mod::SourceId::lfo (0), st::mod::ids::sync));
            s.setValueNotifyingHost (1.0f);
        }, "06_lfo_popover_synced", 1.0f, [find] (juce::Component& ed)
        {
            if (auto* strip = dynamic_cast<st::ModulationStrip*> (find (ed, [] (juce::Component& c) { return dynamic_cast<st::ModulationStrip*> (&c) != nullptr; })))
                strip->showPopover (0);
        } });
        snap->startTimerHz (60);
    }
};

static juce::JUCEApplicationBase* createSnapshotApp() { return new SnapshotApp(); }

int main (int argc, char** argv)
{
    juce::StringArray args (argv + 1, argc - 1);
    if (args.contains ("--snapshot"))
    {
        snapshotDir = juce::File (args[args.indexOf ("--snapshot") + 1]);
        juce::JUCEApplicationBase::createInstance = &createSnapshotApp;
        return juce::JUCEApplicationBase::main (argc, (const char**) argv);
    }
    return runTests();
}
