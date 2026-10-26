#pragma once

#include "PluginProcessor.h"

class DenoiseEditor : public juce::AudioProcessorEditor
{
public:
    explicit DenoiseEditor (DenoiseProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider strengthKnob;
    juce::Label strengthLabel;
    juce::ToggleButton listenButton { "Listen to removed noise" };

    juce::AudioProcessorValueTreeState::SliderAttachment strengthAttachment;
    juce::AudioProcessorValueTreeState::ButtonAttachment listenAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DenoiseEditor)
};
