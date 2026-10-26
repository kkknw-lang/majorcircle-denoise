#include "PluginProcessor.h"
#include "PluginEditor.h"

DenoiseProcessor::DenoiseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "MajorCircleDenoise", createLayout())
{
    strength = params.getRawParameterValue ("strength");
    noiseOnly = params.getRawParameterValue ("noiseOnly");
}

juce::AudioProcessorValueTreeState::ParameterLayout DenoiseProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "strength", 1 }, "Strength",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "noiseOnly", 1 }, "Listen to Noise", false));
    return layout;
}

bool DenoiseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void DenoiseProcessor::prepareToPlay (double sampleRate, int)
{
    for (auto& d : denoisers)
        d.prepare (sampleRate);
    setLatencySamples (mcd::FRAME_SIZE);
}

double DenoiseProcessor::getTailLengthSeconds() const
{
    const double sr = getSampleRate();
    return sr > 0.0 ? mcd::FRAME_SIZE / sr : 0.0;
}

void DenoiseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const double strengthPercent = strength->load();
    const bool listenToNoise = noiseOnly->load() > 0.5f;
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) denoisers.size());

    for (int ch = 0; ch < numChannels; ++ch)
        denoisers[(size_t) ch].process (buffer.getWritePointer (ch), buffer.getNumSamples(), strengthPercent, listenToNoise);
}

juce::AudioProcessorEditor* DenoiseProcessor::createEditor()
{
    return new DenoiseEditor (*this);
}

void DenoiseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DenoiseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (params.state.getType()))
            params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DenoiseProcessor();
}
