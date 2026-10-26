// Standalone checks for DenoiseCore.h (no JUCE): g++ -O2 -std=c++17 -I../Source DenoiseCoreTest.cpp
#include "DenoiseCore.h"

#include <cstdio>
#include <random>

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++failures;
}

static double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) (to - from));
}

// Runs the denoiser in uneven block sizes, like a host would.
static std::vector<float> run (const std::vector<float>& input, double sr, double strength, bool noiseOnly = false)
{
    mcd::ChannelDenoiser d;
    d.prepare (sr);
    std::vector<float> out = input;
    const int blocks[] = { 64, 512, 100, 1024, 333 };
    size_t pos = 0;
    for (int k = 0; pos < out.size(); ++k)
    {
        const int n = (int) std::min<size_t> ((size_t) blocks[k % 5], out.size() - pos);
        d.process (out.data() + pos, n, strength, noiseOnly);
        pos += (size_t) n;
    }
    return out;
}

int main()
{
    const double sr = 48000.0;
    const size_t len = (size_t) (sr * 12);
    const size_t latency = mcd::FRAME_SIZE;

    std::mt19937 rng (1);
    std::normal_distribution<float> gauss (0.0f, 1.0f);

    // 1. Strength 0 reconstructs the input exactly, delayed by FRAME_SIZE.
    {
        std::vector<float> x (len);
        for (auto& v : x)
            v = 0.3f * gauss (rng);
        auto y = run (x, sr, 0.0);
        double maxErr = 0.0;
        for (size_t i = latency; i < len; ++i)
            maxErr = std::max (maxErr, (double) std::abs (y[i] - x[i - latency]));
        std::printf ("      max reconstruction error: %.2e\n", maxErr);
        check (maxErr < 1e-5, "strength 0 = input delayed by 2048 samples");
    }

    // 2. Tone bursts (a sung phrase with gaps) over steady hiss.
    std::vector<float> tone (len, 0.0f), noise (len);
    for (size_t i = 0; i < len; ++i)
    {
        const double t = (double) i / sr;
        const bool on = std::fmod (t, 2.0) < 1.2; // 1.2 s on, 0.8 s off
        tone[i] = on ? (float) (0.3 * std::sin (2.0 * M_PI * 440.0 * t)) : 0.0f;
        noise[i] = 0.01f * gauss (rng);
    }
    std::vector<float> mix (len);
    for (size_t i = 0; i < len; ++i)
        mix[i] = tone[i] + noise[i];

    for (double strength : { 30.0, 60.0, 100.0 })
    {
        auto y = run (mix, sr, strength);
        // A gap in the second half (t = 9.3..9.9 s): only noise left.
        const size_t g0 = (size_t) (9.3 * sr) + latency, g1 = (size_t) (9.9 * sr) + latency;
        const double before = rms (mix, g0 - latency, g1 - latency);
        const double after = rms (y, g0, g1);
        const double reductionDb = 20.0 * std::log10 (before / after);

        // During the tone (t = 8.2..9.0 s): the tone should come through nearly untouched.
        const size_t t0 = (size_t) (8.2 * sr), t1 = (size_t) (9.0 * sr);
        const double toneIn = rms (tone, t0, t1);
        const double toneOut = rms (y, t0 + latency, t1 + latency);
        const double toneLossDb = 20.0 * std::log10 (toneIn / toneOut);

        std::printf ("      strength %3.0f%%: noise -%.1f dB in gaps, tone level change %.2f dB\n",
                     strength, reductionDb, -toneLossDb);
        // The website's own denoise.js gives about -3 dB on this signal (a deliberately
        // gentle floor: 15-32th percentile), so this checks for the same behaviour.
        check (reductionDb > 1.5, "noise in gaps is reduced");
        check (std::abs (toneLossDb) < 1.0, "tone level kept within 1 dB");
    }

    // 3. Noise-only listen mode: wet + removed = dry.
    {
        auto wet = run (mix, sr, 70.0, false);
        auto removed = run (mix, sr, 70.0, true);
        double maxErr = 0.0;
        for (size_t i = latency; i < len; ++i)
            maxErr = std::max (maxErr, (double) std::abs (wet[i] + removed[i] - mix[i - latency]));
        check (maxErr < 1e-5, "noise-only output + denoised output = original");
    }

    // 4. 44.1 kHz works too.
    {
        auto y = run (mix, 44100.0, 50.0);
        bool finite = true;
        for (float v : y)
            finite = finite && std::isfinite (v);
        check (finite, "44.1 kHz output is finite");
    }

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
