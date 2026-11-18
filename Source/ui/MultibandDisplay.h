#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** The hero display: spectrum, soft band "hills", crossover handles, drive handles, output meter.
    Everything animates (hill heights, hover / selection glow, crossover handles). */
class MultibandDisplay : public juce::Component
{
public:
    explicit MultibandDisplay (StSatProcessor&);
    ~MultibandDisplay() override;

    std::function<void (int band)> onBandSelected;
    void setSelectedBand (int b) { selected = b; }
    int getSelectedBand() const noexcept { return selected; }

    enum class AnalyzerMode { off, post, preAndPost };
    void setAnalyzerMode (AnalyzerMode m);

    /** Where the floating band panel sits (local coords) so hills/handles stay readable above it. */
    void setPanelArea (juce::Rectangle<float> r) { panelArea = r; }

    /** X position (local) of a band's centre, for docking the floating panel. */
    float getBandCentreX (int band) const;
    /** Called every frame by the display's clock (lets the editor animate in sync). */
    std::function<void (double dt)> onFrame;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    StSatProcessor& proc;
    st::SpectrumAnalyzer preSpectrum, postSpectrum;
    AnalyzerMode analyzerMode = AnalyzerMode::post;
    int selected = 0;
    juce::Rectangle<float> plot, panelArea;

    // Interaction
    enum class Target { none, crossover, driveHandle, bandBody, removeButton };
    struct Hit { Target target = Target::none; int index = -1; };
    Hit hover, drag;
    float dragStartValue = 0.0f;
    juce::Point<float> dragStart;
    juce::RangedAudioParameter* dragParam = nullptr;

    // Animation
    std::array<st::Animated, kMaxBands> hillHeight, hoverAmt, selectAmt, presence, glow;
    std::array<st::Animated, kMaxCrossovers> xoverHover;
    st::Animated hintAlpha;
    std::array<float, 2> meterDb { -100.0f, -100.0f }, meterHold { -100.0f, -100.0f };
    std::unique_ptr<st::FrameClock> clock;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    float xForHz (float hz) const noexcept;
    float hzForX (float x) const noexcept;
    float heightForDrive (float db) const noexcept;
    float driveForHeight (float h) const noexcept;
    float baseline() const noexcept { return plot.getBottom(); }

    int numBands() const { return proc.getNumBands(); }
    float crossover (int i) const;
    float bandMagnitude (int band, float hz) const;   // analytic LR magnitude (0..1)
    float bandCentreHz (int band) const;
    int bandAtX (float x) const;
    juce::Point<float> driveHandle (int b) const;
    juce::Rectangle<float> labelBounds (int b) const;
    juce::Rectangle<float> removeBounds (int b) const;
    juce::Point<float> crossoverHandle (int i) const;

    Hit hitTestAt (juce::Point<float>) const;
    void frame (double dt);

    void drawBackground (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawHills (juce::Graphics&);
    void drawCrossovers (juce::Graphics&);
    void drawLabels (juce::Graphics&);
    void drawMeter (juce::Graphics&);
};

} // namespace stsat
