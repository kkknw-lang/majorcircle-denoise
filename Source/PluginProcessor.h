#pragma once

#include "DenoiseCore.h"

#include <juce_audio_processors/juce_audio_processors.h>

class DenoiseProcessor : public juce::AudioProcessor
{
public:
    DenoiseProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::String bandParamId (int band) { return "band" + juce::String (band + 1); }
    static juce::String bandName (int band);

    juce::AudioProcessorValueTreeState params;
    mcd::Denoiser denoiser;

    /** Set by the editor's Learn button (not automatable). */
    std::atomic<bool> learning { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<float>* threshold = nullptr;
    std::atomic<float>* reduction = nullptr;
    std::atomic<float>* smoothing = nullptr;
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* noiseOnly = nullptr;
    std::array<std::atomic<float>*, mcd::NUM_BANDS> bands {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DenoiseProcessor)
};
