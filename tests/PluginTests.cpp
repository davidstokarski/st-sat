// Whole-plugin smoke / stress tests and editor snapshots.
#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    struct Job { std::function<void()> setup; juce::String name; float scale = 1.0f; };
    StSatProcessor& p;
    juce::File dir;
    std::vector<Job> jobs;
    size_t index = 0;
    int ticks = 0;
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
        }

        bool f = true;
        runNoise (p, 4, 256, f);

        if (++ticks > 45)
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
    p.removeBand (1);
    check (p.getNumBands() == 2, "removeBand -> 2 bands");

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
        setParam (p, stsat::ids::numBands, 6.0f);
        for (int b = 0; b < 6; ++b) setParam (p, stsat::ids::band (b, "dyn"), 50.0f);
        const int blocks = 400;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runNoise (p, blocks, 512, finite);
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const double audioMs = blocks * 512 / 48.0;
        std::cout << "CPU (6 bands, 4x, dynamics on): " << juce::String (100.0 * ms / audioMs, 1) << "% of one core\n";
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
            p.modMatrix.addRouting (st::mod::SourceId::lfo (0), stsat::ids::band (0, "tone"), -0.4f);
            p.uiState.setProperty ("selectedBand", 0, nullptr);
        }, "03_updown_mod" });
        snap->jobs.push_back ({ {}, "04_small_scale", 0.75f });
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
