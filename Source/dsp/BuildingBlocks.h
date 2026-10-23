// =============================================================================
//  BuildingBlocks.h
//
//  The small DSP "parts" the reverb is assembled from. Think of these like the
//  components on a circuit board: each one does one simple job, and the reverb
//  engine (FdnReverb.h) wires them together.
//
//  Everything here is plain C++ with no JUCE dependency, so it can be unit-tested
//  on its own and reused in other plugins.
// =============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace eighthhall::dsp
{

inline constexpr float pi    = 3.14159265358979323846f;
inline constexpr float twoPi = 2.0f * pi;

// -----------------------------------------------------------------------------
//  DelayLine
//
//  A circular buffer: we write one sample in every tick and can read back any
//  sample from the past. Reading at a *fractional* position (e.g. 1234.37
//  samples ago) uses 4-point cubic Hermite interpolation, which is what lets us
//  smoothly modulate delay times without zipper noise or harsh aliasing.
//
//  The buffer size is rounded up to a power of two so wrapping the index is a
//  cheap bit-mask instead of a modulo/branch.
// -----------------------------------------------------------------------------
class DelayLine
{
public:
    void prepare (std::size_t maxDelaySamples)
    {
        std::size_t size = 1;
        while (size < maxDelaySamples + 4)   // +4 = room for the interpolator
            size <<= 1;

        buffer.assign (size, 0.0f);
        mask     = size - 1;
        writePos = 0;
    }

    void clear() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x) noexcept
    {
        buffer[writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    // Integer read: the sample written `delay` ticks ago (delay >= 1).
    float read (std::size_t delay) const noexcept
    {
        return buffer[(writePos - delay) & mask];
    }

    // Fractional read with cubic Hermite interpolation (delay >= 2).
    float readHermite (float delay) const noexcept
    {
        const auto whole = static_cast<std::size_t> (delay);
        const float frac = delay - static_cast<float> (whole);

        // Four neighbouring samples around the read point, oldest last.
        const float xm1 = buffer[(writePos - whole + 1) & mask];
        const float x0  = buffer[(writePos - whole)     & mask];
        const float x1  = buffer[(writePos - whole - 1) & mask];
        const float x2  = buffer[(writePos - whole - 2) & mask];

        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    std::size_t capacity() const noexcept { return mask; }

private:
    std::vector<float> buffer;
    std::size_t mask = 0, writePos = 0;
};

// -----------------------------------------------------------------------------
//  OnePoleLowpass
//
//  The simplest possible filter: y = y + a * (x - y). Inside the reverb loop it
//  models air and wall absorption - high frequencies die away faster than lows,
//  which is what makes a reverb sound like a real room instead of a metal pipe.
// -----------------------------------------------------------------------------
class OnePoleLowpass
{
public:
    void setCutoff (float hz, float sampleRate) noexcept
    {
        coeff = 1.0f - std::exp (-twoPi * hz / sampleRate);
    }

    // Lets the engine crossfade toward "no filtering" (used by Freeze).
    void setCoefficient (float a) noexcept { coeff = a; }
    float getCoefficient() const noexcept  { return coeff; }

    float process (float x) noexcept
    {
        state += coeff * (x - state);
        return state;
    }

    void reset() noexcept { state = 0.0f; }

private:
    float coeff = 1.0f, state = 0.0f;
};

// -----------------------------------------------------------------------------
//  Allpass (Schroeder allpass diffuser)
//
//  Passes every frequency at equal level but smears it in time. A few of these
//  in a row turn one sharp click into a dense cloud of tiny echoes - this is the
//  "diffusion" stage that gives the reverb a smooth onset instead of a
//  flutter-y, grainy one.
// -----------------------------------------------------------------------------
class Allpass
{
public:
    void prepare (std::size_t delaySamples)
    {
        delay = std::max<std::size_t> (1, delaySamples);
        line.prepare (delay);
    }

    void setGain (float g) noexcept { gain = g; }
    void clear() noexcept           { line.clear(); }

    float process (float x) noexcept
    {
        const float delayed = line.read (delay);
        const float v = x + gain * delayed;
        line.push (v);
        return delayed - gain * v;
    }

private:
    DelayLine line;
    std::size_t delay = 1;
    float gain = 0.5f;
};

// -----------------------------------------------------------------------------
//  SvfFilter - 2nd-order state-variable filter, "TPT" (topology-preserving
//  transform) form, after Vadim Zavalishin's "The Art of VA Filter Design".
//
//  Used for the Low Cut / High Cut controls on the reverb's output. The TPT form
//  stays stable and smooth even while the cutoff is being swept, which matters
//  because those knobs are automatable.
// -----------------------------------------------------------------------------
class SvfFilter
{
public:
    enum class Type { lowpass, highpass };

    void setType (Type t) noexcept { type = t; }

    void setCutoff (float hz, float sampleRate, float q = 0.7071f) noexcept
    {
        hz = std::clamp (hz, 10.0f, 0.49f * sampleRate);
        g  = std::tan (pi * hz / sampleRate);
        k  = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    float process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        const float low = v2;
        if (type == Type::lowpass)
            return low;
        return x - k * v1 - low;   // highpass
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

private:
    Type  type = Type::lowpass;
    float g = 0.0f, k = 1.4142f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// -----------------------------------------------------------------------------
//  SineLfo - slow sine oscillator that gently wobbles the delay times.
//  Tiny, constantly moving delay times break up the metallic "ringing" that
//  static digital reverbs suffer from, and add lushness.
// -----------------------------------------------------------------------------
class SineLfo
{
public:
    void setPhase (float normalised) noexcept { phase = normalised - std::floor (normalised); }

    void setFrequency (float hz, float sampleRate) noexcept { increment = hz / sampleRate; }

    // Advances by `numSamples` and returns the current value (-1..1).
    float advance (int numSamples) noexcept
    {
        phase += increment * static_cast<float> (numSamples);
        phase -= std::floor (phase);
        return std::sin (twoPi * phase);
    }

private:
    float phase = 0.0f, increment = 0.0f;
};

// -----------------------------------------------------------------------------
//  LinearSmoother - glides a value toward its target over a fixed time.
//  Every knob goes through one of these so moving a control (or host
//  automation) never produces clicks.
// -----------------------------------------------------------------------------
class LinearSmoother
{
public:
    void prepare (float sampleRate, float rampSeconds) noexcept
    {
        rampLength = std::max (1, static_cast<int> (sampleRate * rampSeconds));
        steps = 0;
    }

    void setCurrentAndTarget (float v) noexcept { current = target = v; steps = 0; }

    void setTarget (float v) noexcept
    {
        if (! (v < target) && ! (v > target))   // already heading there
            return;
        target = v;
        steps  = rampLength;
        step   = (target - current) / static_cast<float> (steps);
    }

    // Advances by `numSamples` and returns the new value.
    float advance (int numSamples) noexcept
    {
        if (steps <= 0)
            return current;

        if (numSamples >= steps)
        {
            current = target;
            steps = 0;
        }
        else
        {
            current += step * static_cast<float> (numSamples);
            steps   -= numSamples;
        }
        return current;
    }

    float getCurrent() const noexcept { return current; }
    bool isSmoothing() const noexcept { return steps > 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int rampLength = 1, steps = 0;
};

} // namespace eighthhall::dsp
