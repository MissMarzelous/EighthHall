// =============================================================================
//  Presets.h - factory presets. Hosts show these in their preset/program menu,
//  and the plugin's own preset box lists them too.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace eighthhall
{
struct Preset
{
    const char* name;
    // Values in the parameters' real units (%, ms, s, Hz) - see Parameters.h.
    float mix, preDelay, size, decay, damping, diffusion, modRate, modDepth, lowCut, highCut, width;
};

const std::vector<Preset>& getFactoryPresets();

// Moves every parameter to the preset's values and tells the host about it,
// so the change is recorded by automation/undo.
void applyPreset (const Preset&, juce::AudioProcessorValueTreeState&);
} // namespace eighthhall
