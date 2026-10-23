#include "DecayDisplay.h"
#include "EighthHallLookAndFeel.h"
#include "../Parameters.h"

#include <cmath>

namespace eighthhall::gui
{
using namespace juce;

namespace
{
constexpr float minDb = -72.0f;

// Mirrors the engine's size curve and average delay-line length
// (see FdnReverb.h) so the graph matches what you hear.
constexpr float averageLineMs = 51.5f;
float sizeToScale (float size01) { return 0.25f + 1.5f * size01 * size01; }

float niceTimeStep (float span)
{
    for (float step : { 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f })
        if (span / step <= 7.0f)
            return step;
    return 10.0f;
}
} // namespace

// =============================================================================
DecayDisplay::DecayDisplay (AudioProcessorValueTreeState& s) : state (s)
{
    shown = read();
    startTimerHz (30);
}

DecayDisplay::Snapshot DecayDisplay::read() const
{
    namespace id = params::id;
    Snapshot snap;
    snap.preDelayMs = state.getRawParameterValue (id::preDelay)->load();
    snap.decay      = state.getRawParameterValue (id::decay)->load();
    snap.damping    = state.getRawParameterValue (id::damping)->load();
    snap.size       = state.getRawParameterValue (id::size)->load() * 0.01f;
    snap.diffusion  = state.getRawParameterValue (id::diffusion)->load() * 0.01f;
    snap.freeze     = state.getRawParameterValue (id::freeze)->load() >= 0.5f;
    return snap;
}

void DecayDisplay::timerCallback()
{
    const auto now = read();
    if (now != shown)
    {
        shown = now;
        repaint();
    }
}

float DecayDisplay::estimateHighDecay (const Snapshot& s)
{
    constexpr float fs = 48000.0f, f = 8000.0f;
    const float a = 1.0f - std::exp (-MathConstants<float>::twoPi * s.damping / fs);
    const float w = MathConstants<float>::twoPi * f / fs;
    // Magnitude of y += a (x - y) at frequency w
    const float mag = a / std::sqrt (1.0f - 2.0f * (1.0f - a) * std::cos (w) + (1.0f - a) * (1.0f - a));
    const float lossDbPerTrip = -20.0f * std::log10 (jmax (1.0e-6f, mag));
    const float tripSeconds = averageLineMs * 0.001f * sizeToScale (s.size);

    const float midRate  = 60.0f / jmax (0.01f, s.decay);   // dB per second
    const float highRate = midRate + lossDbPerTrip / tripSeconds;
    return 60.0f / highRate;
}

void DecayDisplay::paint (Graphics& g)
{
    const auto outer = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (outer, 10.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (outer.reduced (0.5f), 10.0f, 1.0f);

    auto plot = outer.reduced (14.0f, 12.0f);
    plot.removeFromTop (22.0f);      // header text
    plot.removeFromBottom (16.0f);   // time labels
    plot.removeFromLeft (30.0f);     // dB labels

    const auto s = shown;
    const float preSec  = s.preDelayMs * 0.001f;
    const float highRt  = estimateHighDecay (s);
    const float span    = s.freeze ? 4.0f : jmax (0.5f, (preSec + s.decay) * 1.15f);
    const float step    = niceTimeStep (span);
    const float tMax    = std::ceil (span / step) * step;

    auto xFor = [&] (float t)  { return plot.getX() + plot.getWidth() * jlimit (0.0f, 1.0f, t / tMax); };
    auto yFor = [&] (float db) { return plot.getY() + plot.getHeight() * jlimit (0.0f, 1.0f, db / minDb); };

    // ---- grid ---------------------------------------------------------------
    g.setFont (FontOptions (11.0f));
    for (float db : { 0.0f, -20.0f, -40.0f, -60.0f })
    {
        const float y = yFor (db);
        g.setColour (colours::track.withAlpha (db < -59.0f ? 0.9f : 0.55f));
        g.drawHorizontalLine (roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (colours::textDim);
        g.drawText (String (roundToInt (db)), Rectangle<float> (plot.getX() - 32.0f, y - 7.0f, 28.0f, 14.0f),
                    Justification::centredRight);
    }
    for (float t = 0.0f; t <= tMax + 1.0e-4f; t += step)
    {
        const float x = xFor (t);
        g.setColour (colours::track.withAlpha (0.35f));
        g.drawVerticalLine (roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (colours::textDim);
        g.drawText (params::formatSeconds (t), Rectangle<float> (x - 24.0f, plot.getBottom() + 2.0f, 48.0f, 14.0f),
                    Justification::centred);
    }

    // ---- envelopes ----------------------------------------------------------
    // The tail builds up over a few tens of ms (longer for big, diffuse rooms),
    // then falls in a straight line in dB - the hallmark of exponential decay.
    const float buildUp = 0.01f + 0.05f * sizeToScale (s.size) * (0.4f + 0.6f * s.diffusion);

    auto envelope = [&] (float rt60)
    {
        Path p;
        const float t0 = preSec;
        p.startNewSubPath (xFor (t0), yFor (minDb));
        p.lineTo (xFor (t0 + buildUp), yFor (0.0f));
        if (s.freeze)
        {
            p.lineTo (xFor (tMax), yFor (0.0f));
        }
        else
        {
            const float tEnd = t0 + buildUp + rt60 * (-minDb / 60.0f);
            p.lineTo (xFor (tEnd), yFor (minDb));
        }
        return p;
    };

    // Highs (drawn first, underneath)
    if (! s.freeze)
    {
        auto highs = envelope (highRt);
        g.setColour (colours::accentWarm.withAlpha (0.85f));
        Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        PathStrokeType (1.6f).createDashedStroke (dashed, highs, dashes, 2);
        g.fillPath (dashed);
    }

    // Mids: filled area + outline
    auto mids = envelope (s.decay);
    Path fill (mids);
    fill.lineTo (xFor (s.freeze ? tMax : preSec), yFor (minDb));
    fill.closeSubPath();
    g.setGradientFill (ColourGradient (colours::accent.withAlpha (0.35f), 0.0f, plot.getY(),
                                       colours::accent.withAlpha (0.02f), 0.0f, plot.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colours::accent);
    g.strokePath (mids, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));

    // Pre-delay marker
    if (preSec > 0.0005f)
    {
        g.setColour (colours::textDim.withAlpha (0.6f));
        const float x = xFor (preSec);
        const float dashes[] = { 2.0f, 3.0f };
        g.drawDashedLine ({ x, plot.getY(), x, plot.getBottom() }, dashes, 2, 1.0f);
    }

    // ---- header text ----------------------------------------------------------
    auto header = outer.reduced (16.0f, 10.0f).removeFromTop (20.0f);
    g.setFont (FontOptions (13.0f, Font::bold));
    g.setColour (colours::text);
    const String title = s.freeze ? "FROZEN  -  tail held indefinitely"
                                  : "RT60  " + params::formatSeconds (s.decay)
                                        + "     PRE-DELAY  " + params::formatSeconds (preSec);
    g.drawText (title, header, Justification::centredLeft);

    if (! s.freeze)
    {
        g.setFont (FontOptions (12.0f));
        auto legend = header;
        g.setColour (colours::accentWarm);
        g.drawText ("- - Highs (8 kHz) " + params::formatSeconds (highRt), legend.removeFromRight (170.0f), Justification::centredRight);
        g.setColour (colours::accent);
        g.drawText ("Mids", legend.removeFromRight (60.0f), Justification::centredRight);
    }
}

// =============================================================================
LevelMeter::LevelMeter (std::array<std::atomic<float>, 2>& p) : peaks (p)
{
    startTimerHz (30);
}

void LevelMeter::timerCallback()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const float peak = peaks[ch].exchange (0.0f);
        const float db = Decibels::gainToDecibels (peak, -100.0f);

        // Instant attack, smooth ~24 dB/s release - like a PPM-style meter.
        levelDb[ch] = db > levelDb[ch] ? db : levelDb[ch] - 0.8f;

        if (db >= holdDb[ch]) { holdDb[ch] = db; holdTicks[ch] = 45; }
        else if (--holdTicks[ch] <= 0) holdDb[ch] -= 1.2f;
    }
    repaint();
}

void LevelMeter::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::panelEdge);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);

    auto inner = r.reduced (8.0f, 12.0f);
    auto labelArea = inner.removeFromBottom (14.0f);
    const float barW = (inner.getWidth() - 4.0f) * 0.5f;

    auto toY = [&] (float db) { return inner.getBottom() - inner.getHeight() * jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f); };

    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto bar = Rectangle<float> (inner.getX() + (float) ch * (barW + 4.0f), inner.getY(), barW, inner.getHeight());
        g.setColour (colours::track);
        g.fillRoundedRectangle (bar, 2.0f);

        const float top = toY (levelDb[ch]);
        g.setGradientFill (ColourGradient (Colour (0xffff5a5a), 0.0f, toY (3.0f),
                                           colours::accent, 0.0f, toY (-18.0f), false));
        g.fillRoundedRectangle (bar.withTop (top), 2.0f);

        if (holdDb[ch] > -60.0f)
        {
            g.setColour (holdDb[ch] > 0.0f ? Colour (0xffff5a5a) : colours::text);
            g.fillRect (bar.withTop (toY (holdDb[ch])).withHeight (2.0f));
        }
    }

    g.setColour (colours::textDim);
    g.setFont (FontOptions (10.0f));
    g.drawText ("L   R", labelArea, Justification::centred);
}
} // namespace eighthhall::gui
