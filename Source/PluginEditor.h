#pragma once

#include "PluginProcessor.h"

/** Live spectrum: input, the noise threshold, and the denoised output. */
class SpectrumView : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumView (DenoiseProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Horizontal position of a frequency, shared with the band sliders. */
    static float freqToX (double freq, float width);

private:
    void timerCallback() override;
    juce::Path makeCurve (const std::vector<float>& db, bool closed) const;

    DenoiseProcessor& processor;
    mcd::Analysis analysis;
    std::vector<float> inputDb, outputDb, thresholdDb;
    bool hasThreshold = false;
};

class DenoiseEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DenoiseEditor (DenoiseProcessor&);
    ~DenoiseEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void learnClicked();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    DenoiseProcessor& processor;
    juce::LookAndFeel_V4 lookAndFeel;

    SpectrumView spectrum;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::ComboBox modeBox;
    juce::TextButton learnButton { "LEARN" };
    juce::ToggleButton listenButton { "Listen to removed noise" };
    juce::Label profileStatus;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };
    std::array<Knob, 3> knobs;
    std::array<Knob, mcd::NUM_BANDS> bandSliders;

    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> listenAttachment;

    juce::Rectangle<int> spectrumArea, bandArea, knobArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DenoiseEditor)
};
