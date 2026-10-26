#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff15161a };
const juce::Colour panel { 0xff202228 };
const juce::Colour accent { 0xff7c5cff };
const juce::Colour text { 0xffe8e8ee };
const juce::Colour subtle { 0xff9a9aa8 };
} // namespace

DenoiseEditor::DenoiseEditor (DenoiseProcessor& p)
    : AudioProcessorEditor (p),
      strengthAttachment (p.params, "strength", strengthKnob),
      listenAttachment (p.params, "noiseOnly", listenButton)
{
    strengthKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    strengthKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 22);
    strengthKnob.setTextValueSuffix (" %");
    strengthKnob.setColour (juce::Slider::rotarySliderFillColourId, accent);
    strengthKnob.setColour (juce::Slider::rotarySliderOutlineColourId, panel.brighter (0.15f));
    strengthKnob.setColour (juce::Slider::thumbColourId, text);
    strengthKnob.setColour (juce::Slider::textBoxTextColourId, text);
    strengthKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (strengthKnob);

    strengthLabel.setText ("STRENGTH", juce::dontSendNotification);
    strengthLabel.setJustificationType (juce::Justification::centred);
    strengthLabel.setColour (juce::Label::textColourId, subtle);
    addAndMakeVisible (strengthLabel);

    listenButton.setColour (juce::ToggleButton::textColourId, text);
    listenButton.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (listenButton);

    setSize (320, 300);
}

void DenoiseEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    g.setColour (text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("MajorCircle Denoise", 20, 14, getWidth() - 40, 26, juce::Justification::left);

    g.setColour (subtle);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("Steady hiss / hum / room-tone reduction", 20, 40, getWidth() - 40, 18, juce::Justification::left);

    g.setColour (panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (20.0f, 68.0f, (float) getWidth() - 40.0f, 170.0f), 10.0f);
}

void DenoiseEditor::resized()
{
    strengthLabel.setBounds (20, 76, getWidth() - 40, 18);
    strengthKnob.setBounds ((getWidth() - 140) / 2, 94, 140, 138);
    listenButton.setBounds (20, 252, getWidth() - 40, 28);
}
