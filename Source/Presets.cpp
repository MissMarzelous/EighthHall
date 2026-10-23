#include "Presets.h"
#include "Parameters.h"

namespace eighthhall
{
const std::vector<Preset>& getFactoryPresets()
{
    //                         mix  pre   size decay damp    diff  rate  depth lowcut hicut  width
    static const std::vector<Preset> presets {
        { "Default",           30,  20,   60,  2.5f,  7000,   75,   0.60f, 35,   120,   12000, 100 },
        { "Tight Room",        22,   4,   20,  0.6f,  6000,   80,   0.40f, 15,   180,   10000,  80 },
        { "Vocal Plate",       28,  35,   45,  1.8f,  11000,  90,   0.90f, 30,   220,   14000, 110 },
        { "Drum Chamber",      25,   8,   35,  1.1f,  8000,   70,   0.50f, 20,   150,   11000, 100 },
        { "Concert Hall",      35,  28,   75,  3.2f,  6500,   80,   0.50f, 35,   90,    10000, 110 },
        { "Cathedral",         45,  60,   95,  8.5f,  4500,   85,   0.35f, 45,   70,     8000, 125 },
        { "Ambient Wash",      55,  80,   100, 14.0f, 5000,   90,   0.25f, 75,   150,    9000, 150 },
        { "Bright Bloom",      60,  45,   85,  10.0f, 16000,  95,   1.40f, 85,   300,   18000, 170 },
        { "Dark Space",        40,  30,   80,  6.0f,  1800,   75,   0.30f, 40,   60,     4000, 120 },
    };
    return presets;
}

void applyPreset (const Preset& p, juce::AudioProcessorValueTreeState& state)
{
    auto set = [&state] (const char* pid, float value)
    {
        if (auto* param = state.getParameter (pid))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (value));
            param->endChangeGesture();
        }
    };

    namespace id = params::id;
    set (id::mix, p.mix);           set (id::preDelay, p.preDelay);
    set (id::size, p.size);         set (id::decay, p.decay);
    set (id::damping, p.damping);   set (id::diffusion, p.diffusion);
    set (id::modRate, p.modRate);   set (id::modDepth, p.modDepth);
    set (id::lowCut, p.lowCut);     set (id::highCut, p.highCut);
    set (id::width, p.width);       set (id::freeze, 0.0f);
}
} // namespace eighthhall
