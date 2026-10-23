// =============================================================================
//  Parameters.h - every knob the plugin exposes to the host (your DAW).
//
//  Parameter IDs are permanent: once a version is released, never rename one,
//  or users' saved sessions will lose their settings. The number after each ID
//  in ParameterID{ "mix", 1 } is a version hint used by AU/VST3 hosts.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace eighthhall::params
{
namespace id
{
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* preDelay  = "predelay";
    inline constexpr const char* size      = "size";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* damping   = "damping";
    inline constexpr const char* diffusion = "diffusion";
    inline constexpr const char* modRate   = "modrate";
    inline constexpr const char* modDepth  = "moddepth";
    inline constexpr const char* lowCut    = "lowcut";
    inline constexpr const char* highCut   = "highcut";
    inline constexpr const char* width     = "width";
    inline constexpr const char* freeze    = "freeze";
}

// ---- how values are shown as text in the GUI and in the host --------------
inline juce::String formatHz (float hz)
{
    if (hz >= 1000.0f) return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    if (hz < 10.0f)    return juce::String (hz, 2) + " Hz";   // e.g. modulation rate
    return juce::String (juce::roundToInt (hz)) + " Hz";
}

inline juce::String formatSeconds (float s)
{
    return s < 1.0f ? juce::String (juce::roundToInt (s * 1000.0f)) + " ms"
                    : juce::String (s, s < 10.0f ? 2 : 1) + " s";
}

inline juce::NormalisableRange<float> skewed (float min, float max, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (min, max, step);
    r.setSkewForCentre (centre);   // puts `centre` at 12 o'clock on the knob
    return r;
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (const char* pid, const char* name, float def, float max = 100.0f)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { pid, 1 }, name, NormalisableRange<float> (0.0f, max, 0.1f), def,
            AudioParameterFloatAttributes().withLabel ("%")
                .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + "%"; })
                .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); }));
    };

    auto hz = [] (const char* pid, const char* name, NormalisableRange<float> range, float def)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { pid, 1 }, name, range, def,
            AudioParameterFloatAttributes().withLabel ("Hz")
                .withStringFromValueFunction ([] (float v, int) { return formatHz (v); })
                .withValueFromStringFunction ([] (const String& t)
                {
                    const float v = t.getFloatValue();
                    return t.containsIgnoreCase ("k") ? v * 1000.0f : v;
                }));
    };

    auto seconds = [] (const char* pid, const char* name, NormalisableRange<float> range, float def, bool storedInMs)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { pid, 1 }, name, range, def,
            AudioParameterFloatAttributes().withLabel (storedInMs ? "ms" : "s")
                .withStringFromValueFunction ([storedInMs] (float v, int) { return formatSeconds (storedInMs ? v * 0.001f : v); })
                .withValueFromStringFunction ([storedInMs] (const String& t)
                {
                    float v = t.getFloatValue();
                    const bool typedMs = t.containsIgnoreCase ("ms");
                    if (storedInMs && ! typedMs && t.containsIgnoreCase ("s")) v *= 1000.0f;
                    if (! storedInMs && typedMs) v *= 0.001f;
                    return v;
                }));
    };

    layout.add (percent (id::mix,       "Mix",       30.0f));
    layout.add (seconds (id::preDelay,  "Pre-Delay", skewed (0.0f, 250.0f, 40.0f, 0.1f), 20.0f, true));
    layout.add (percent (id::size,      "Size",      60.0f));
    layout.add (seconds (id::decay,     "Decay",     skewed (0.2f, 20.0f, 2.5f, 0.01f), 2.5f, false));
    layout.add (hz      (id::damping,   "Damping",   skewed (1000.0f, 20000.0f, 5000.0f, 1.0f), 7000.0f));
    layout.add (percent (id::diffusion, "Diffusion", 75.0f));
    layout.add (hz      (id::modRate,   "Mod Rate",  skewed (0.05f, 5.0f, 0.8f, 0.01f), 0.6f));
    layout.add (percent (id::modDepth,  "Mod Depth", 35.0f));
    layout.add (hz      (id::lowCut,    "Low Cut",   skewed (20.0f, 1000.0f, 150.0f, 1.0f), 120.0f));
    layout.add (hz      (id::highCut,   "High Cut",  skewed (1000.0f, 20000.0f, 6000.0f, 1.0f), 12000.0f));
    layout.add (percent (id::width,     "Width",     100.0f, 200.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { id::freeze, 1 }, "Freeze", false));

    return layout;
}
} // namespace eighthhall::params
