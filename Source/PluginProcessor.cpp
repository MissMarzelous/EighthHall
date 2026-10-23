#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "Presets.h"

using namespace eighthhall;

EighthHallAudioProcessor::EighthHallAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "EighthHallState", params::createLayout())
{
    auto get = [this] (const char* pid)
    {
        auto* p = state.getRawParameterValue (pid);
        jassert (p != nullptr);   // a typo in a parameter ID would trip this in Debug builds
        return p;
    };

    namespace id = params::id;
    pMix       = get (id::mix);
    pPreDelay  = get (id::preDelay);
    pSize      = get (id::size);
    pDecay     = get (id::decay);
    pDamping   = get (id::damping);
    pDiffusion = get (id::diffusion);
    pModRate   = get (id::modRate);
    pModDepth  = get (id::modDepth);
    pLowCut    = get (id::lowCut);
    pHighCut   = get (id::highCut);
    pWidth     = get (id::width);
    pFreeze    = get (id::freeze);
}

// -----------------------------------------------------------------------------
bool EighthHallAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output must be stereo; input can be mono (mono-to-stereo reverb) or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void EighthHallAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Called before playback starts (and whenever the sample rate or buffer
    // size changes). The only place we're allowed to allocate memory.
    reverb.prepare (sampleRate, samplesPerBlock);
    reverb.setParameters (readParameters());
}

void EighthHallAudioProcessor::reset()
{
    reverb.reset();
}

eighthhall::dsp::ReverbParameters EighthHallAudioProcessor::readParameters() const noexcept
{
    dsp::ReverbParameters p;
    p.mix        = pMix->load()       * 0.01f;
    p.preDelayMs = pPreDelay->load();
    p.size       = pSize->load()      * 0.01f;
    p.decaySec   = pDecay->load();
    p.dampingHz  = pDamping->load();
    p.diffusion  = pDiffusion->load() * 0.01f;
    p.modRateHz  = pModRate->load();
    p.modDepth   = pModDepth->load()  * 0.01f;
    p.lowCutHz   = pLowCut->load();
    p.highCutHz  = pHighCut->load();
    p.width      = pWidth->load()     * 0.01f;
    p.freeze     = pFreeze->load() >= 0.5f;
    return p;
}

void EighthHallAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Tiny "denormal" numbers at the very end of a reverb tail can make some
    // CPUs run 100x slower. This flushes them to zero for the whole block.
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numInputs   = getTotalNumInputChannels();
    const int numOutputs  = getTotalNumOutputChannels();

    if (numSamples == 0 || numOutputs < 2)
        return;

    // Mono input: duplicate it into the right channel so the engine always
    // sees stereo.
    if (numInputs == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    else if (numInputs == 0)
        buffer.clear();   // input bus disabled: don't process leftover garbage

    reverb.setParameters (readParameters());
    reverb.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    // Feed the output meters (keep the highest peak until the GUI reads it).
    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, numSamples);
        float previous = outputPeak[(size_t) ch].load (std::memory_order_relaxed);
        while (peak > previous && ! outputPeak[(size_t) ch].compare_exchange_weak (previous, peak))
            ;
    }
}

double EighthHallAudioProcessor::getTailLengthSeconds() const
{
    if (pFreeze->load() >= 0.5f)
        return std::numeric_limits<double>::infinity();
    return pDecay->load() * 1.25 + pPreDelay->load() * 0.001;
}

// ---- presets -------------------------------------------------------------------
int EighthHallAudioProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

void EighthHallAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentPreset = index;
    applyPreset (presets[(size_t) index], state);
}

const juce::String EighthHallAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? presets[(size_t) index].name : "";
}

// ---- state -------------------------------------------------------------------
void EighthHallAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("presetIndex", currentPreset, nullptr);
    tree.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void EighthHallAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentPreset = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("presetIndex", 0));
            state.replaceState (tree);

            // replaceState() skips parameters whose stored value "looks" the
            // same - e.g. an on/off switch the host left at 0.25 (which reads
            // as "off"). Re-applying every value guarantees the plugin ends up
            // exactly as it was saved.
            for (auto* param : getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                    if (auto child = tree.getChildWithProperty ("id", ranged->getParameterID()); child.isValid())
                        ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) child.getProperty ("value")));
        }
    }
}

// ---- editor ---------------------------------------------------------------------
juce::AudioProcessorEditor* EighthHallAudioProcessor::createEditor()
{
    return new EighthHallAudioProcessorEditor (*this);
}

// JUCE calls this to create the plugin when the host loads it.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EighthHallAudioProcessor();
}
