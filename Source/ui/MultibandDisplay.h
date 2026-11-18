#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** The hero display: spectrum analyser, band regions, crossover handles and per-band drive handles. */
class MultibandDisplay : public juce::Component, private juce::Timer
{
public:
    explicit MultibandDisplay (StSatProcessor&);
    ~MultibandDisplay() override;

    std::function<void (int band)> onBandSelected;
    void setSelectedBand (int b) { selected = b; repaint(); }
    int getSelectedBand() const noexcept { return selected; }

    enum class AnalyzerMode { off, post, preAndPost };
    void setAnalyzerMode (AnalyzerMode m);

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
    double lastTick = 0.0;

    juce::Rectangle<float> plot;      // analyser / band area
    juce::Rectangle<float> labelsRow; // frequency labels

    // Interaction state
    enum class Target { none, crossover, driveHandle, bandBody, removeButton };
    struct Hit { Target target = Target::none; int index = -1; };
    Hit hover, drag;
    float dragStartValue = 0.0f;
    juce::Point<float> dragStart;
    juce::RangedAudioParameter* dragParam = nullptr;

    // Animated per-band glow (smoothed output level)
    std::array<float, kMaxBands> glow {};
    std::array<float, kMaxBands> bandAnim {};

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    float xForHz (float hz) const noexcept;
    float hzForX (float x) const noexcept;
    float yForDrive (float db) const noexcept;
    float driveForY (float y) const noexcept;

    int numBands() const { return proc.getNumBands(); }
    float crossover (int i) const;
    juce::Range<float> bandXRange (int b) const;
    int bandAtX (float x) const;
    juce::Rectangle<float> pillBounds (int b) const;
    juce::Rectangle<float> removeBounds (int b) const;
    juce::Point<float> driveHandle (int b) const;

    Hit hitTestAt (juce::Point<float>) const;
    void timerCallback() override;

    void drawGrid (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawBands (juce::Graphics&);
    void drawCrossovers (juce::Graphics&);
};

} // namespace stsat
