// =============================================================================
//  FdnReverb.h  -  the Eighth Hall reverb engine
//
//  Signal flow (per stereo sample):
//
//    in L/R ──► pre-delay ──► 4 allpass diffusers ──┐
//                                                  ▼
//               ┌─────────────── 8-line Feedback Delay Network ──────────────┐
//               │  each line:  modulated delay ─► damping lowpass ─► gain g_i │
//               │  all 8 lines are then mixed by a Householder matrix and fed │
//               │  back into themselves (plus the new input)                  │
//               └────────────────────────────────────────────────────────────┘
//                                                  ▼
//          8 lines tapped with two orthogonal sign patterns ─► wet L / wet R
//                                                  ▼
//               Low Cut ─► High Cut ─► stereo Width ─► dry/wet Mix ─► out
//
//  Why a Feedback Delay Network (FDN)?
//    It is the architecture behind many modern studio reverbs (Jot & Chaigne,
//    1991). Several delay lines feed back into each other through a "lossless"
//    mixing matrix, so echoes multiply into a dense, smooth tail. Because the
//    matrix itself never adds or removes energy, the *only* thing controlling
//    how fast the tail dies is the gain g_i on each line - which means we can
//    set the decay time (RT60) exactly:
//
//         g_i = 10 ^ ( -3 * delaySeconds_i / RT60 )
//
//    i.e. every time sound travels around a line it drops by exactly the right
//    number of dB to reach -60 dB after RT60 seconds. The test program checks
//    this against measured impulse responses.
//
//  Real-time safety: prepare() is the only place that allocates memory.
//  process() never allocates, locks, or does file/console I/O, so it is safe to
//  call on the audio thread.
// =============================================================================
#pragma once

#include "BuildingBlocks.h"

#include <array>
#include <cmath>

namespace eighthhall::dsp
{

struct ReverbParameters
{
    float mix        = 0.30f;    // 0..1      dry / wet balance
    float preDelayMs = 20.0f;    // 0..250 ms gap before the reverb starts
    float size       = 0.60f;    // 0..1      scales all delay-line lengths (room size)
    float decaySec   = 2.5f;     // 0.2..20 s RT60: time for the tail to fall 60 dB
    float dampingHz  = 7000.0f;  // 1k..20k   how quickly highs die inside the tail
    float diffusion  = 0.75f;    // 0..1      density of the early build-up
    float modRateHz  = 0.6f;     // 0.05..5   speed of the delay-time wobble
    float modDepth   = 0.35f;    // 0..1      amount of wobble
    float lowCutHz   = 120.0f;   // 20..1000  high-pass on the wet signal
    float highCutHz  = 12000.0f; // 1k..20k   low-pass on the wet signal
    float width      = 1.0f;     // 0..2      0 = mono, 1 = natural, 2 = extra wide
    bool  freeze     = false;    // hold the current tail forever
};

class FdnReverb
{
public:
    static constexpr std::size_t numLines = 8;

    void prepare (double newSampleRate, int /*maxBlockSize*/)
    {
        sampleRate = static_cast<float> (newSampleRate);

        // ---- pre-delay ------------------------------------------------------
        const auto maxPreDelay = static_cast<std::size_t> (0.26f * sampleRate) + 8;
        for (auto& d : preDelay)
            d.prepare (maxPreDelay);

        // ---- input diffusers (slightly different per channel = wider image) -
        for (std::size_t ch = 0; ch < 2; ++ch)
            for (std::size_t i = 0; i < numDiffusers; ++i)
                diffusers[ch][i].prepare (msToSamples (diffuserMs[ch][i]));

        // ---- FDN delay lines ----------------------------------------------
        const float maxLineMs = baseLineMs[numLines - 1] * maxSizeScale + maxModMs + 1.0f;
        for (auto& line : lines)
            line.prepare (msToSamples (maxLineMs));

        // Spread the LFO phases and rates so the lines never wobble in sync.
        for (std::size_t i = 0; i < numLines; ++i)
            lfos[i].setPhase (static_cast<float> (i) / static_cast<float> (numLines));

        // ---- smoothing ramps --------------------------------------------------
        mixSmoother      .prepare (sampleRate, 0.05f);
        widthSmoother    .prepare (sampleRate, 0.05f);
        preDelaySmoother .prepare (sampleRate, 0.25f);
        sizeSmoother     .prepare (sampleRate, 0.40f);
        freezeSmoother   .prepare (sampleRate, 0.10f);

        reset();
        initialised = false;   // snap smoothers to the first parameters we receive
    }

    void reset()
    {
        for (auto& d : preDelay) d.clear();
        for (auto& ch : diffusers) for (auto& ap : ch) ap.clear();
        for (auto& l : lines) l.clear();
        for (auto& f : damping) f.reset();
        for (auto& f : lowCut)  f.reset();
        for (auto& f : highCut) f.reset();
    }

    void setParameters (const ReverbParameters& p) noexcept
    {
        params = p;

        if (! initialised)
        {
            mixSmoother     .setCurrentAndTarget (p.mix);
            widthSmoother   .setCurrentAndTarget (p.width);
            preDelaySmoother.setCurrentAndTarget (p.preDelayMs);
            sizeSmoother    .setCurrentAndTarget (sizeToScale (p.size));
            freezeSmoother  .setCurrentAndTarget (p.freeze ? 1.0f : 0.0f);
            initialised = true;
        }

        mixSmoother     .setTarget (p.mix);
        widthSmoother   .setTarget (p.width);
        preDelaySmoother.setTarget (p.preDelayMs);
        sizeSmoother    .setTarget (sizeToScale (p.size));
        freezeSmoother  .setTarget (p.freeze ? 1.0f : 0.0f);
    }

    // In-place stereo processing. `left`/`right` may point to the same buffer
    // for mono use.
    void process (float* left, float* right, int numSamples) noexcept
    {
        int done = 0;
        while (done < numSamples)
        {
            // Coefficients are recalculated every `controlRate` samples (about
            // 0.3 ms at 48 kHz) - fast enough to be inaudible, cheap enough to
            // leave lots of CPU headroom.
            const int n = std::min (controlRate, numSamples - done);
            updateCoefficients (n);
            processChunk (left + done, right + done, n);
            done += n;
        }
    }

    // A rough, conservative tail length for the host (used for offline bounces).
    double getTailSeconds() const noexcept
    {
        return params.freeze ? 1.0e6 : static_cast<double> (params.decaySec * 1.25f + params.preDelayMs * 0.001f);
    }

private:
    // ----- fixed design constants ------------------------------------------
    static constexpr std::size_t numDiffusers = 4;
    static constexpr int controlRate  = 16;

    // Delay lengths in ms at "size = 1x". Chosen to be mutually incommensurate
    // (no shared factors) so echoes never line up into audible pitches.
    static constexpr std::array<float, numLines> baseLineMs
        { 29.71f, 37.13f, 41.11f, 43.73f, 53.27f, 59.93f, 67.69f, 79.31f };

    static constexpr float diffuserMs[2][numDiffusers]
        { { 4.771f, 3.595f, 12.73f, 9.307f },
          { 4.913f, 3.719f, 13.11f, 9.071f } };

    static constexpr float minSizeScale = 0.25f;
    static constexpr float maxSizeScale = 1.75f;
    static constexpr float maxModMs     = 0.65f;

    // Output tap signs: two rows of an 8x8 Hadamard matrix. They are orthogonal,
    // so the left and right outputs are decorrelated (a wide, natural stereo
    // image from a mono source).
    static constexpr std::array<float, numLines> tapL {  1,  1,  1,  1, -1, -1, -1, -1 };
    static constexpr std::array<float, numLines> tapR {  1, -1,  1, -1,  1, -1,  1, -1 };

    // Where each channel's input enters the network.
    static constexpr std::array<float, numLines> injectL { 1, 0, -1, 0, 1, 0, -1, 0 };
    static constexpr std::array<float, numLines> injectR { 0, 1, 0, -1, 0, 1, 0, -1 };

    static float sizeToScale (float size) noexcept
    {
        // Squared curve: more resolution in the small-room end of the knob.
        const float s = std::clamp (size, 0.0f, 1.0f);
        return minSizeScale + (maxSizeScale - minSizeScale) * s * s;
    }

    std::size_t msToSamples (float ms) const noexcept
    {
        return static_cast<std::size_t> (std::ceil (ms * 0.001f * sampleRate));
    }

    // ----- per-chunk control update ------------------------------------------
    void updateCoefficients (int n) noexcept
    {
        const float freeze  = freezeSmoother.advance (n);
        const float scale   = sizeSmoother.advance (n);
        mixNow              = mixSmoother.advance (n);
        widthNow            = widthSmoother.advance (n);
        preDelaySamplesNow  = std::max (0.0f, preDelaySmoother.advance (n) * 0.001f * sampleRate);
        inputGainNow        = 1.0f - freeze;

        const float rt60       = std::clamp (params.decaySec, 0.05f, 100.0f);
        const float modSamples = params.modDepth * params.modDepth * maxModMs * 0.001f * sampleRate;
        const float dampCoeff  = 1.0f - std::exp (-twoPi * std::clamp (params.dampingHz, 200.0f, 0.49f * sampleRate) / sampleRate);

        for (std::size_t i = 0; i < numLines; ++i)
        {
            // Each LFO runs at a slightly different rate (+/- ~15%).
            lfos[i].setFrequency (params.modRateHz * (0.85f + 0.043f * static_cast<float> (i)), sampleRate);
            const float wobble = lfos[i].advance (n) * modSamples;

            lineDelay[i] = std::max (4.0f, baseLineMs[i] * scale * 0.001f * sampleRate + wobble);

            // The RT60 formula from the header comment, using this line's
            // actual current length.
            const float g = std::pow (10.0f, -3.0f * lineDelay[i] / (rt60 * sampleRate));

            // Freeze crossfades toward a perfectly lossless loop.
            lineGain[i] = g + freeze * (1.0f - g);
            damping[i].setCoefficient (dampCoeff + freeze * (1.0f - dampCoeff));
        }

        const float diffGain = 0.72f * std::clamp (params.diffusion, 0.0f, 1.0f);
        for (auto& ch : diffusers)
            for (auto& ap : ch)
                ap.setGain (diffGain);

        for (std::size_t ch = 0; ch < 2; ++ch)
        {
            lowCut[ch].setType (SvfFilter::Type::highpass);
            highCut[ch].setType (SvfFilter::Type::lowpass);
            lowCut[ch].setCutoff (params.lowCutHz, sampleRate);
            highCut[ch].setCutoff (params.highCutHz, sampleRate);
        }
    }

    // ----- the audio loop ----------------------------------------------------
    void processChunk (float* left, float* right, int n) noexcept
    {
        // Equal-power dry/wet: the overall loudness stays even across the knob.
        const float dryGain = std::cos (0.5f * pi * mixNow);
        const float wetGain = std::sin (0.5f * pi * mixNow);

        // Normalisation so the wet signal sits at a sensible level.
        constexpr float outputScale = 0.35f;
        const float halfWidth = 0.5f * widthNow;

        for (int s = 0; s < n; ++s)
        {
            const float dryL = left[s];
            const float dryR = right[s];

            // 1) Pre-delay
            preDelay[0].push (dryL * inputGainNow);
            preDelay[1].push (dryR * inputGainNow);
            float inL = preDelaySamplesNow < 2.0f ? preDelay[0].read (1) : preDelay[0].readHermite (preDelaySamplesNow);
            float inR = preDelaySamplesNow < 2.0f ? preDelay[1].read (1) : preDelay[1].readHermite (preDelaySamplesNow);

            // 2) Input diffusion
            for (std::size_t i = 0; i < numDiffusers; ++i)
            {
                inL = diffusers[0][i].process (inL);
                inR = diffusers[1][i].process (inR);
            }

            // 3) Read every line, damp it, apply its decay gain
            std::array<float, numLines> y {};
            float sum = 0.0f;
            for (std::size_t i = 0; i < numLines; ++i)
            {
                y[i] = damping[i].process (lines[i].readHermite (lineDelay[i])) * lineGain[i];
                sum += y[i];
            }

            // 4) Stereo output taps
            float wetL = 0.0f, wetR = 0.0f;
            for (std::size_t i = 0; i < numLines; ++i)
            {
                wetL += tapL[i] * y[i];
                wetR += tapR[i] * y[i];
            }

            // 5) Householder feedback matrix:  out = y - (2/N) * sum(y)
            //    It's orthogonal (energy-preserving) and needs only O(N) math.
            const float householder = (2.0f / static_cast<float> (numLines)) * sum;
            for (std::size_t i = 0; i < numLines; ++i)
                lines[i].push (y[i] - householder + injectL[i] * inL + injectR[i] * inR);

            // 6) Wet EQ
            wetL = highCut[0].process (lowCut[0].process (wetL * outputScale));
            wetR = highCut[1].process (lowCut[1].process (wetR * outputScale));

            // 7) Width (mid/side)
            const float mid  = 0.5f * (wetL + wetR);
            const float side = halfWidth * (wetL - wetR);
            wetL = mid + side;
            wetR = mid - side;

            // 8) Mix
            left[s]  = dryGain * dryL + wetGain * wetL;
            right[s] = dryGain * dryR + wetGain * wetR;
        }
    }

    // ----- state ---------------------------------------------------------------
    float sampleRate = 48000.0f;
    ReverbParameters params;
    bool initialised = false;

    std::array<DelayLine, 2> preDelay;
    std::array<std::array<Allpass, numDiffusers>, 2> diffusers;
    std::array<DelayLine, numLines> lines;
    std::array<OnePoleLowpass, numLines> damping;
    std::array<SineLfo, numLines> lfos;
    std::array<SvfFilter, 2> lowCut, highCut;

    std::array<float, numLines> lineDelay {}, lineGain {};

    LinearSmoother mixSmoother, widthSmoother, preDelaySmoother, sizeSmoother, freezeSmoother;
    float mixNow = 0.0f, widthNow = 1.0f, preDelaySamplesNow = 0.0f, inputGainNow = 1.0f;
};

} // namespace eighthhall::dsp
