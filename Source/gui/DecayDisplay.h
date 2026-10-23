// =============================================================================
//  DecayDisplay.h - the graph at the top of the plugin.
//
//  Draws how loud the reverb tail is over time (in dB) for the current
//  settings: the pre-delay gap, the build-up, and two decay slopes - one for
//  the mid frequencies (what the Decay knob sets) and one for the highs (how
//  much faster the Damping control makes treble die away).
//
//  LevelMeter - a simple stereo output peak meter.
// =============================================================================
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>

namespace eighthhall::gui
{
class DecayDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit DecayDisplay (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;

private:
    struct Snapshot
    {
        float preDelayMs = 0, decay = 0, damping = 0, size = 0, diffusion = 0;
        bool freeze = false;
        bool operator!= (const Snapshot& o) const noexcept
        {
            using juce::exactlyEqual;
            return ! exactlyEqual (preDelayMs, o.preDelayMs) || ! exactlyEqual (decay, o.decay)
                || ! exactlyEqual (damping, o.damping) || ! exactlyEqual (size, o.size)
                || ! exactlyEqual (diffusion, o.diffusion) || freeze != o.freeze;
        }
    };

    void timerCallback() override;
    Snapshot read() const;

    // Estimated RT60 at 8 kHz, from the damping filter's loss per trip around
    // the feedback loop.
    static float estimateHighDecay (const Snapshot&);

    juce::AudioProcessorValueTreeState& state;
    Snapshot shown;
};

class LevelMeter final : public juce::Component, private juce::Timer
{
public:
    explicit LevelMeter (std::array<std::atomic<float>, 2>& peaksToRead);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    std::array<std::atomic<float>, 2>& peaks;
    std::array<float, 2> levelDb { -100.0f, -100.0f };
    std::array<float, 2> holdDb  { -100.0f, -100.0f };
    std::array<int, 2>   holdTicks { 0, 0 };
};
} // namespace eighthhall::gui
