// =============================================================================
//  ReverbTests.cpp - objective checks for the Eighth Hall reverb engine
//
//  Run:     EighthHallTests                 (runs all tests, exit code 0 = pass)
//           EighthHallTests --render <dir>  (also writes demo WAV files for your
//                                         website / portfolio)
//
//  The tests treat the reverb the way a QA bench would treat hardware: feed it
//  known signals and measure what comes out.
// =============================================================================
#include "dsp/FdnReverb.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace eighthhall::dsp;

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

struct Stereo
{
    std::vector<float> l, r;
    explicit Stereo (std::size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
    std::size_t size() const { return l.size(); }
};

// Runs a whole buffer through the engine in host-sized blocks.
void run (FdnReverb& rv, Stereo& io, int blockSize = 256)
{
    for (std::size_t pos = 0; pos < io.size(); pos += static_cast<std::size_t> (blockSize))
    {
        const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize), io.size() - pos));
        rv.process (io.l.data() + pos, io.r.data() + pos, n);
    }
}

ReverbParameters neutralWet (float decay)
{
    ReverbParameters p;
    p.mix = 1.0f;            // 100% wet (sin(pi/2) = 1)
    p.preDelayMs = 0.0f;
    p.decaySec = decay;
    p.dampingHz = 20000.0f;  // as little damping as possible
    p.modDepth = 0.0f;
    p.lowCutHz = 20.0f;
    p.highCutHz = 20000.0f;
    p.width = 1.0f;
    return p;
}

// Schroeder backward integration -> energy decay curve -> RT60 via T30
// (fit between -5 dB and -35 dB, extrapolated to -60 dB, as in ISO 3382).
double measureRt60 (const Stereo& ir, double sampleRate)
{
    // Like an acoustician's measurement, we analyse the 1 kHz octave band
    // (707 Hz - 1.41 kHz). Highs are *supposed* to die faster (that's the
    // Damping control), so the Decay knob is defined as the mid-band RT60.
    Stereo band = ir;
    for (auto* ch : { &band.l, &band.r })
    {
        SvfFilter hp1, hp2, lp1, lp2;
        hp1.setType (SvfFilter::Type::highpass); hp2.setType (SvfFilter::Type::highpass);
        hp1.setCutoff (707.0f, float (sampleRate)); hp2.setCutoff (707.0f, float (sampleRate));
        lp1.setCutoff (1414.0f, float (sampleRate)); lp2.setCutoff (1414.0f, float (sampleRate));
        for (auto& v : *ch)
            v = lp2.process (lp1.process (hp2.process (hp1.process (v))));
    }

    std::vector<double> edc (band.size());
    double acc = 0.0;
    for (std::size_t i = band.size(); i-- > 0;)
    {
        acc += double (band.l[i]) * band.l[i] + double (band.r[i]) * band.r[i];
        edc[i] = acc;
    }
    const double total = edc[0];
    if (total <= 0.0)
        return 0.0;

    // Least-squares line through EDC(dB) between -5 and -35 dB.
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int count = 0;
    for (std::size_t i = 0; i < edc.size(); ++i)
    {
        const double db = 10.0 * std::log10 (edc[i] / total + 1e-30);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = double (i) / sampleRate;
            sx += t; sy += db; sxx += t * t; sxy += t * db;
            ++count;
        }
    }
    if (count < 10)
        return 0.0;
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);   // dB per second
    return -60.0 / slope;
}

Stereo impulseResponse (double sampleRate, const ReverbParameters& p, double seconds)
{
    FdnReverb rv;
    rv.prepare (sampleRate, 512);
    rv.setParameters (p);
    Stereo io (static_cast<std::size_t> (seconds * sampleRate));
    io.l[0] = io.r[0] = 1.0f;
    run (rv, io);
    return io;
}

bool allFinite (const Stereo& s, float limit, float* peakOut = nullptr)
{
    float peak = 0.0f;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (! std::isfinite (s.l[i]) || ! std::isfinite (s.r[i]))
            return false;
        peak = std::max ({ peak, std::abs (s.l[i]), std::abs (s.r[i]) });
    }
    if (peakOut) *peakOut = peak;
    return peak < limit;
}

double rms (const std::vector<float>& v, std::size_t from, std::size_t to)
{
    double a = 0.0;
    for (std::size_t i = from; i < to; ++i)
        a += double (v[i]) * v[i];
    return std::sqrt (a / double (std::max<std::size_t> (1, to - from)));
}

// ---- tests ---------------------------------------------------------------

void testDecayAccuracy()
{
    std::puts ("\nDecay time accuracy (measured RT60 vs. Decay knob, 48 kHz)");
    for (float target : { 0.5f, 1.0f, 2.5f, 6.0f })
    {
        const auto ir = impulseResponse (48000.0, neutralWet (target), target * 1.6 + 0.5);
        const double measured = measureRt60 (ir, 48000.0);
        const double err = 100.0 * (measured - target) / target;
        char msg[128];
        std::snprintf (msg, sizeof msg, "Decay %.1f s -> measured %.2f s (%+.1f%%)", target, measured, err);
        check (std::abs (err) < 5.0, msg);
    }
}

void testSampleRateIndependence()
{
    std::puts ("\nSample-rate independence (Decay = 2.0 s)");
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        const auto ir = impulseResponse (sr, neutralWet (2.0f), 3.6);
        const double measured = measureRt60 (ir, sr);
        char msg[128];
        std::snprintf (msg, sizeof msg, "%6.0f Hz -> RT60 %.2f s", sr, measured);
        check (std::abs (measured - 2.0) < 0.1, msg);
    }
}

void testMixZeroIsTransparent()
{
    std::puts ("\nBypass behaviour");
    FdnReverb rv;
    rv.prepare (48000.0, 512);
    auto p = neutralWet (3.0f);
    p.mix = 0.0f;
    rv.setParameters (p);

    std::mt19937 rng (1);
    std::uniform_real_distribution<float> dist (-1.0f, 1.0f);
    Stereo io (48000), ref (48000);
    for (std::size_t i = 0; i < io.size(); ++i)
        ref.l[i] = io.l[i] = dist (rng), ref.r[i] = io.r[i] = dist (rng);
    run (rv, io);

    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < io.size(); ++i)
        maxDiff = std::max ({ maxDiff, std::abs (io.l[i] - ref.l[i]), std::abs (io.r[i] - ref.r[i]) });
    check (maxDiff < 1.0e-6f, "Mix 0% passes the dry signal through unchanged");
}

void testStability()
{
    std::puts ("\nStability under random settings and loud noise (200 runs x 2 s)");
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    std::uniform_int_distribution<int> blockDist (1, 2048);

    bool ok = true;
    float worstPeak = 0.0f;
    for (int run_ = 0; run_ < 200 && ok; ++run_)
    {
        const double sr = std::array<double, 4> { 44100.0, 48000.0, 96000.0, 192000.0 }[run_ % 4];
        ReverbParameters p;
        p.mix        = u (rng);
        p.preDelayMs = 250.0f * u (rng);
        p.size       = u (rng);
        p.decaySec   = 0.2f + 19.8f * u (rng) * u (rng);
        p.dampingHz  = 1000.0f + 19000.0f * u (rng);
        p.diffusion  = u (rng);
        p.modRateHz  = 0.05f + 4.95f * u (rng);
        p.modDepth   = u (rng);
        p.lowCutHz   = 20.0f + 980.0f * u (rng);
        p.highCutHz  = 1000.0f + 19000.0f * u (rng);
        p.width      = 2.0f * u (rng);
        p.freeze     = u (rng) < 0.15f;

        FdnReverb rv;
        rv.prepare (sr, 2048);
        rv.setParameters (p);

        Stereo io (static_cast<std::size_t> (2.0 * sr));
        std::normal_distribution<float> noise (0.0f, 0.5f);
        for (std::size_t i = 0; i < io.size() / 2; ++i)
            io.l[i] = noise (rng), io.r[i] = noise (rng);

        // Process with random block sizes and change parameters mid-stream,
        // like a host automating knobs.
        std::size_t pos = 0;
        while (pos < io.size())
        {
            const int n = static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockDist (rng)), io.size() - pos));
            rv.process (io.l.data() + pos, io.r.data() + pos, n);
            pos += static_cast<std::size_t> (n);
            if (u (rng) < 0.05f) { p.size = u (rng); p.preDelayMs = 250.0f * u (rng); rv.setParameters (p); }
        }

        float peak = 0.0f;
        ok = allFinite (io, 40.0f, &peak);
        worstPeak = std::max (worstPeak, peak);
    }
    char msg[128];
    std::snprintf (msg, sizeof msg, "No NaN/Inf, output bounded (worst peak %.1f for 0.5-RMS noise in)", worstPeak);
    check (ok, msg);
}

void testFreeze()
{
    FdnReverb rv;
    rv.prepare (48000.0, 512);
    auto p = neutralWet (1.0f);
    p.modDepth = 0.3f;
    rv.setParameters (p);

    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.0f, 0.3f);

    Stereo feed (48000);
    for (std::size_t i = 0; i < feed.size(); ++i)
        feed.l[i] = noise (rng), feed.r[i] = noise (rng);
    run (rv, feed);

    p.freeze = true;
    rv.setParameters (p);

    Stereo held (48000 * 10);
    for (std::size_t i = 0; i < held.size(); ++i)   // new input must be ignored
        held.l[i] = noise (rng), held.r[i] = noise (rng);
    run (rv, held);

    const double early = rms (held.l, 48000, 96000);       // 1-2 s after freeze
    const double late  = rms (held.l, 8 * 48000, 9 * 48000); // 8-9 s after freeze
    const double dropDb = 20.0 * std::log10 (late / early);
    char msg[128];
    std::snprintf (msg, sizeof msg, "Tail held after 8 s of freeze (change %.1f dB)", dropDb);
    check (dropDb > -6.0 && dropDb < 1.0, msg);

    p.freeze = false;
    p.decaySec = 0.5f;
    rv.setParameters (p);
    Stereo after (48000 * 3);
    run (rv, after);
    check (rms (after.l, 2 * 48000, 3 * 48000) < 1.0e-3 * early, "Tail decays away again when freeze is released");
}

void testStereoDecorrelation()
{
    std::puts ("\nStereo image");
    const auto ir = impulseResponse (48000.0, neutralWet (2.0f), 2.0);
    double lr = 0, ll = 0, rr = 0;
    for (std::size_t i = 4800; i < ir.size(); ++i)   // skip first 100 ms
    {
        lr += double (ir.l[i]) * ir.r[i];
        ll += double (ir.l[i]) * ir.l[i];
        rr += double (ir.r[i]) * ir.r[i];
    }
    const double corr = lr / std::sqrt (ll * rr);
    char msg[128];
    std::snprintf (msg, sizeof msg, "L/R tail correlation from a mono impulse = %.3f (wide stereo)", corr);
    check (std::abs (corr) < 0.3, msg);

    auto mono = neutralWet (2.0f);
    mono.width = 0.0f;
    const auto irMono = impulseResponse (48000.0, mono, 1.0);
    float diff = 0.0f;
    for (std::size_t i = 0; i < irMono.size(); ++i)
        diff = std::max (diff, std::abs (irMono.l[i] - irMono.r[i]));
    check (diff < 1.0e-6f, "Width 0% collapses the reverb to mono");
}

void testPerformance()
{
    std::puts ("\nCPU cost");
    FdnReverb rv;
    rv.prepare (48000.0, 512);
    ReverbParameters p;
    rv.setParameters (p);

    Stereo io (48000 * 20);
    std::mt19937 rng (3);
    std::normal_distribution<float> noise (0.0f, 0.2f);
    for (std::size_t i = 0; i < io.size(); ++i)
        io.l[i] = noise (rng), io.r[i] = noise (rng);

    const auto t0 = std::chrono::steady_clock::now();
    run (rv, io, 512);
    const auto t1 = std::chrono::steady_clock::now();
    const double secs = std::chrono::duration<double> (t1 - t0).count();
    const double realtimeFactor = 20.0 / secs;
    char msg[160];
    std::snprintf (msg, sizeof msg, "20 s of stereo audio in %.3f s = %.0fx faster than real time (~%.2f%% of one core)",
                   secs, realtimeFactor, 100.0 / realtimeFactor);
    check (realtimeFactor > 20.0, msg);
}

// ---- demo rendering (for the website) --------------------------------------

// Writes a stereo WAV: 16-bit PCM for listening, or 32-bit float for analysis
// (float keeps the quiet end of a reverb tail that 16-bit would round to zero).
void writeWav (const std::string& path, const Stereo& s, int sampleRate, bool asFloat = false)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (std::uint32_t v) { f.write (reinterpret_cast<const char*> (&v), 4); };
    auto u16 = [&] (std::uint16_t v) { f.write (reinterpret_cast<const char*> (&v), 2); };

    const std::uint16_t bytesPerSample = asFloat ? 4 : 2;
    const auto frames   = static_cast<std::uint32_t> (s.size());
    const auto dataSize = frames * 2u * bytesPerSample;

    f.write ("RIFF", 4); u32 (36 + dataSize); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (asFloat ? 3 : 1); u16 (2); u32 (static_cast<std::uint32_t> (sampleRate));
    u32 (static_cast<std::uint32_t> (sampleRate) * 2u * bytesPerSample);
    u16 (static_cast<std::uint16_t> (2 * bytesPerSample)); u16 (static_cast<std::uint16_t> (8 * bytesPerSample));
    f.write ("data", 4); u32 (dataSize);

    for (std::size_t i = 0; i < s.size(); ++i)
        for (float v : { s.l[i], s.r[i] })
        {
            if (asFloat)
                f.write (reinterpret_cast<const char*> (&v), 4);
            else
                u16 (static_cast<std::uint16_t> (static_cast<std::int16_t> (std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f))));
        }
}

// A short synthetic phrase: plucked notes (Karplus-Strong) plus a snare-like hit.
Stereo makeDryPhrase (int sr)
{
    Stereo s (static_cast<std::size_t> (sr * 7));
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);

    const float notes[] = { 220.0f, 277.18f, 329.63f, 440.0f, 329.63f, 277.18f };
    for (int n = 0; n < 6; ++n)
    {
        const auto start = static_cast<std::size_t> (n * 0.42 * sr);
        const auto period = static_cast<std::size_t> (sr / notes[n]);
        std::vector<float> ks (period);
        for (auto& v : ks) v = u (rng);
        for (std::size_t i = 0; i < static_cast<std::size_t> (sr * 0.9) && start + i < s.size(); ++i)
        {
            const std::size_t k = i % period;
            const float out = ks[k];
            ks[k] = 0.996f * 0.5f * (ks[k] + ks[(k + 1) % period]);
            const float pan = n % 2 ? 0.35f : 0.65f;
            s.l[start + i] += 0.35f * out * (1.0f - pan);
            s.r[start + i] += 0.35f * out * pan;
        }
    }
    const auto snareAt = static_cast<std::size_t> (2.8 * sr);
    for (std::size_t i = 0; i < static_cast<std::size_t> (sr * 0.25); ++i)
    {
        const float env = std::exp (-float (i) / (0.04f * sr));
        const float body = std::sin (twoPi * 185.0f * float (i) / float (sr)) * std::exp (-float (i) / (0.015f * sr));
        const float v = 0.5f * env * u (rng) + 0.4f * body;
        s.l[snareAt + i] += v;
        s.r[snareAt + i] += v;
    }
    return s;
}

void renderDemos (const std::string& dir)
{
    constexpr int sr = 48000;
    const Stereo dry = makeDryPhrase (sr);
    writeWav (dir + "/demo_dry.wav", dry, sr);

    struct Demo { const char* name; ReverbParameters p; };
    ReverbParameters room, hall, ambient;
    room.size = 0.25f;  room.decaySec = 0.8f;  room.preDelayMs = 5.0f;  room.mix = 0.30f; room.dampingHz = 6000.0f;
    hall.size = 0.75f;  hall.decaySec = 3.2f;  hall.preDelayMs = 28.0f; hall.mix = 0.35f;
    ambient.size = 1.0f; ambient.decaySec = 12.0f; ambient.preDelayMs = 60.0f; ambient.mix = 0.5f;
    ambient.modDepth = 0.7f; ambient.dampingHz = 5000.0f; ambient.width = 1.4f;

    for (const Demo& d : { Demo { "room", room }, Demo { "hall", hall }, Demo { "ambient", ambient } })
    {
        FdnReverb rv;
        rv.prepare (sr, 512);
        rv.setParameters (d.p);
        Stereo wet = dry;
        run (rv, wet);
        writeWav (dir + "/demo_" + d.name + ".wav", wet, sr);
    }

    // A raw impulse response, handy for plotting or loading into a convolver.
    auto irParams = neutralWet (2.5f);
    irParams.size = 0.7f;
    irParams.dampingHz = 8000.0f;
    irParams.modDepth = 0.35f;
    writeWav (dir + "/impulse_response_hall.wav", impulseResponse (sr, irParams, 3.5), sr, true);
    std::printf ("\nDemo audio written to %s\n", dir.c_str());
}
} // namespace

int main (int argc, char** argv)
{
    std::puts ("Eighth Hall reverb engine - test bench");

    testDecayAccuracy();
    testSampleRateIndependence();
    testMixZeroIsTransparent();
    testStability();
    std::puts ("\nFreeze");
    testFreeze();
    testStereoDecorrelation();
    testPerformance();

    if (argc >= 3 && std::string (argv[1]) == "--render")
        renderDemos (argv[2]);

    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
