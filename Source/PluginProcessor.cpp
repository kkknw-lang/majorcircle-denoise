#include "PluginProcessor.h"
#include "PluginEditor.h"

DenoiseProcessor::DenoiseProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "MajorCircleDenoise", createLayout())
{
    threshold = params.getRawParameterValue ("threshold");
    reduction = params.getRawParameterValue ("reduction");
    smoothing = params.getRawParameterValue ("smoothing");
    mode = params.getRawParameterValue ("mode");
    noiseOnly = params.getRawParameterValue ("noiseOnly");
    for (int b = 0; b < mcd::NUM_BANDS; ++b)
        bands[(size_t) b] = params.getRawParameterValue (bandParamId (b));
}

juce::String DenoiseProcessor::bandName (int band)
{
    const double f = mcd::BAND_FREQS[band];
    return f >= 1000.0 ? juce::String (f / 1000.0, f < 10000.0 && std::fmod (f, 1000.0) != 0.0 ? 1 : 0) + "k"
                       : juce::String ((int) f);
}

juce::AudioProcessorValueTreeState::ParameterLayout DenoiseProcessor::createLayout()
{
    using Range = juce::NormalisableRange<float>;
    auto db = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto percent = juce::AudioParameterFloatAttributes().withLabel ("%");

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "threshold", 2 }, "Threshold",
                                                             Range (-10.0f, 20.0f, 0.1f), 3.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reduction", 2 }, "Reduction",
                                                             Range (0.0f, 60.0f, 0.1f), 20.0f, db));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "smoothing", 2 }, "Smoothing",
                                                             Range (0.0f, 100.0f, 1.0f), 50.0f, percent));
    for (int b = 0; b < mcd::NUM_BANDS; ++b)
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { bandParamId (b), 2 },
                                                                 "Band " + bandName (b) + " Hz",
                                                                 Range (0.0f, 100.0f, 1.0f), 100.0f, percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "mode", 2 }, "Noise Profile",
                                                              juce::StringArray { "Auto", "Learned" }, 0));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "noiseOnly", 2 }, "Listen to Noise", false));
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
    denoiser.prepare (sampleRate, juce::jlimit (1, mcd::MAX_CHANNELS, getMainBusNumOutputChannels()));
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

    mcd::Params p;
    p.thresholdDb = threshold->load();
    p.reductionDb = reduction->load();
    p.smoothing = smoothing->load();
    for (int b = 0; b < mcd::NUM_BANDS; ++b)
        p.bandAmount[(size_t) b] = bands[(size_t) b]->load();
    p.useLearned = mode->load() > 0.5f;
    p.learning = learning.load();
    p.noiseOnly = noiseOnly->load() > 0.5f;
    denoiser.setParams (p);

    if (buffer.getNumChannels() >= juce::jlimit (1, mcd::MAX_CHANNELS, getMainBusNumOutputChannels()))
        denoiser.process (buffer.getArrayOfWritePointers(), buffer.getNumSamples());
}

juce::AudioProcessorEditor* DenoiseProcessor::createEditor()
{
    return new DenoiseEditor (*this);
}

void DenoiseProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    const auto profile = denoiser.getLearnedProfile();
    state.setProperty ("noiseProfile",
                       profile.empty() ? juce::String()
                                       : juce::MemoryBlock (profile.data(), profile.size() * sizeof (float)).toBase64Encoding(),
                       nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void DenoiseProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    juce::MemoryBlock block;
    if (block.fromBase64Encoding (state.getProperty ("noiseProfile").toString()) && block.getSize() > 0)
    {
        std::vector<float> profile (block.getSize() / sizeof (float));
        std::memcpy (profile.data(), block.getData(), profile.size() * sizeof (float));
        denoiser.setLearnedProfile (profile);
    }
    else
    {
        denoiser.clearLearnedProfile();
    }
    state.removeProperty ("noiseProfile", nullptr);
    params.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DenoiseProcessor();
}
