#pragma once
#include "../PluginProcessor.h"

namespace stsat
{

/** Full-window band display (Saturn-style): warm spectrum line, dashed crossover lines with grip
    buttons, a level line + pill handle per band (drag = Drive), red-lit selected band column and
    modulation dots. Everything eases (selection glow, hover, handle motion). */
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

    /** Vertical range used for band handles (local y): top of travel and bottom of travel. */
    void setHandleRange (float topY, float bottomY) { handleTop = topY; handleBottom = bottomY; }

    float getBandCentreX (int band) const;
    juce::Range<float> getBandXRange (int band) const;
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
    juce::Rectangle<float> plot;
    float handleTop = 90.0f, handleBottom = 300.0f;

    enum class Target { none, crossover, handle, bandBody };
    struct Hit { Target target = Target::none; int index = -1; };
    Hit hover, drag;
    float dragStartValue = 0.0f;
    juce::Point<float> dragStart;
    juce::RangedAudioParameter* dragParam = nullptr;

    std::array<st::Animated, kMaxBands> handleY, hoverAmt, selectAmt;
    std::array<st::Animated, kMaxCrossovers> xoverHover, xoverPos;
    st::Animated hintAlpha;
    std::unique_ptr<st::FrameClock> clock;

    static constexpr float minHz = 20.0f, maxHz = 20000.0f;
    float xForHz (float hz) const noexcept;
    float hzForX (float x) const noexcept;
    float yForDrive (float db) const noexcept;
    float driveForY (float y) const noexcept;

    int numBands() const { return proc.getNumBands(); }
    float crossover (int i) const;
    int bandAtX (float x) const;
    juce::Rectangle<float> handleBounds (int b) const;
    juce::Rectangle<float> gripBounds (int i) const;

    Hit hitTestAt (juce::Point<float>) const;
    void frame (double dt);

    void drawColumns (juce::Graphics&);
    void drawSpectrum (juce::Graphics&);
    void drawCrossovers (juce::Graphics&);
    void drawHandles (juce::Graphics&);
    void drawModDots (juce::Graphics&);
};

} // namespace stsat
