// =============================================================================
//  PluginProcessor.h
//
//  The "AudioProcessor" is the part of a plugin the DAW talks to. It:
//    - declares the parameters (knobs) the host can automate
//    - receives blocks of audio in processBlock() and hands them to the DSP
//    - saves/restores settings with the user's session
//
//  It knows nothing about the GUI - the editor (PluginEditor.h) reads from it,
//  never the other way round. That separation is what lets the plugin run
//  headless in render farms and pass validators like pluginval.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/FdnReverb.h"

#include <array>
#include <atomic>

class EighthHallAudioProcessor final : public juce::AudioProcessor
{
public:
    EighthHallAudioProcessor();
    ~EighthHallAudioProcessor() override = default;

    // ---- audio ---------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // ---- editor --------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // ---- info ----------------------------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // ---- presets ("programs" in host-speak) -----------------------------------
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    // ---- session save / load ---------------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- shared with the editor -----------------------------------------------
    juce::AudioProcessorValueTreeState state;

    // Output peak levels for the meters. Written by the audio thread, read and
    // reset by the GUI thread - std::atomic makes that safe without locks.
    std::array<std::atomic<float>, 2> outputPeak { 0.0f, 0.0f };

private:
    eighthhall::dsp::ReverbParameters readParameters() const noexcept;

    eighthhall::dsp::FdnReverb reverb;
    int currentPreset = 0;

    // Direct pointers to each parameter's live value (fast, lock-free reads on
    // the audio thread).
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pPreDelay = nullptr;
    std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pDamping = nullptr;
    std::atomic<float>* pDiffusion = nullptr;
    std::atomic<float>* pModRate = nullptr;
    std::atomic<float>* pModDepth = nullptr;
    std::atomic<float>* pLowCut = nullptr;
    std::atomic<float>* pHighCut = nullptr;
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pFreeze = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EighthHallAudioProcessor)
};
