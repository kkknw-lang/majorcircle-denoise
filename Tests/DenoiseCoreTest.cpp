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

static double db (double ratio) { return 20.0 * std::log10 (ratio); }

using Stereo = std::array<std::vector<float>, 2>;

// Runs the denoiser in uneven block sizes, like a host would. `during` can change params mid-run.
template <typename Fn>
static Stereo run (const Stereo& input, double sr, mcd::Params params, Fn&& during, int numChannels = 2)
{
    static mcd::Denoiser d; // large object: keep it off the stack
    d.prepare (sr, numChannels);
    d.clearLearnedProfile();
    Stereo out = input;
    const int blocks[] = { 64, 512, 100, 1024, 333 };
    size_t pos = 0;
    for (int k = 0; pos < out[0].size(); ++k)
    {
        const int n = (int) std::min<size_t> ((size_t) blocks[k % 5], out[0].size() - pos);
        during (params, (double) pos / sr);
        d.setParams (params);
        float* ptrs[2] = { out[0].data() + pos, out[1].data() + pos };
        d.process (ptrs, n);
        pos += (size_t) n;
    }
    return out;
}

static Stereo run (const Stereo& input, double sr, mcd::Params params)
{
    return run (input, sr, params, [] (mcd::Params&, double) {});
}

// Noise power in a frequency range (Hz) of x[from..to), via averaged FFT frames.
static double bandPower (const std::vector<float>& x, size_t from, size_t to, double sr, double f0, double f1)
{
    mcd::FFT fft (mcd::FRAME_SIZE);
    std::vector<double> re (mcd::FRAME_SIZE), im (mcd::FRAME_SIZE);
    double sum = 0.0;
    int frames = 0;
    for (size_t s = from; s + mcd::FRAME_SIZE <= to; s += mcd::FRAME_SIZE)
    {
        for (int i = 0; i < mcd::FRAME_SIZE; ++i)
        {
            re[(size_t) i] = x[s + (size_t) i] * (0.5 - 0.5 * std::cos (2.0 * M_PI * i / mcd::FRAME_SIZE));
            im[(size_t) i] = 0.0;
        }
        fft.forward (re.data(), im.data());
        for (int b = (int) (f0 / sr * mcd::FRAME_SIZE); b < (int) (f1 / sr * mcd::FRAME_SIZE); ++b)
            sum += re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b];
        ++frames;
    }
    return sum / frames;
}

int main()
{
    const double sr = 48000.0;
    const size_t len = (size_t) (sr * 12);
    const size_t L = mcd::FRAME_SIZE; // latency

    std::mt19937 rng (1);
    std::normal_distribution<float> gauss (0.0f, 1.0f);

    // 1. Reduction 0 dB reconstructs the input exactly, delayed by FRAME_SIZE.
    {
        Stereo x { std::vector<float> (len), std::vector<float> (len) };
        for (auto& ch : x)
            for (auto& v : ch)
                v = 0.3f * gauss (rng);
        mcd::Params p;
        p.reductionDb = 0.0;
        auto y = run (x, sr, p);
        double maxErr = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (size_t i = L; i < len; ++i)
                maxErr = std::max (maxErr, (double) std::abs (y[(size_t) ch][i] - x[(size_t) ch][i - L]));
        std::printf ("      max reconstruction error: %.2e\n", maxErr);
        check (maxErr < 1e-5, "reduction 0 dB = input delayed by 2048 samples");
    }

    // Test signal: the first 1 s is noise only, then a 'sung' phrase (440 Hz + harmonics)
    // 1.2 s on / 0.8 s off, over steady hiss on both channels.
    Stereo tone { std::vector<float> (len, 0.0f), std::vector<float> (len, 0.0f) };
    Stereo mix = tone;
    for (size_t i = 0; i < len; ++i)
    {
        const double t = (double) i / sr;
        const bool on = t >= 1.0 && std::fmod (t, 2.0) < 1.2;
        double v = 0.0;
        if (on)
            for (int h = 1; h <= 6; ++h)
                v += 0.3 / h * std::sin (2.0 * M_PI * 440.0 * h * t);
        for (int ch = 0; ch < 2; ++ch)
        {
            tone[(size_t) ch][i] = (float) v;
            mix[(size_t) ch][i] = (float) v + 0.01f * gauss (rng);
        }
    }

    const size_t g0 = (size_t) (9.3 * sr), g1 = (size_t) (9.9 * sr); // a gap (noise only)
    const size_t t0 = (size_t) (8.2 * sr), t1 = (size_t) (9.0 * sr); // inside a phrase
    auto gapReduction = [&] (const Stereo& y) { return db (rms (mix[0], g0, g1) / rms (y[0], g0 + L, g1 + L)); };
    auto toneChange = [&] (const Stereo& y) { return db (rms (y[0], t0 + L, t1 + L) / rms (mix[0], t0, t1)); };

    // 2. Default settings: v1 managed about -3 dB on this kind of signal.
    {
        mcd::Params p;
        auto y = run (mix, sr, p);
        std::printf ("      default (Auto): noise -%.1f dB in gaps, phrase level change %+.2f dB\n", gapReduction (y), toneChange (y));
        check (gapReduction (y) > 14.0, "default settings reduce noise in gaps by more than 14 dB");
        check (std::abs (toneChange (y)) < 1.0, "phrase level kept within 1 dB");
    }

    // 3. Reduction knob scales the depth.
    for (double r : { 6.0, 12.0, 30.0, 48.0 })
    {
        mcd::Params p;
        p.reductionDb = r;
        auto y = run (mix, sr, p);
        std::printf ("      reduction %2.0f dB: noise -%.1f dB in gaps, phrase %+.2f dB\n", r, gapReduction (y), toneChange (y));
        check (gapReduction (y) > r * 0.6, "gap reduction follows the Reduction knob");
        check (std::abs (toneChange (y)) < 1.0, "phrase level kept within 1 dB");
    }

    // 4. Learn: learn the first second (noise only), then use the learned profile.
    {
        mcd::Params p;
        p.reductionDb = 30.0;
        auto y = run (mix, sr, p, [] (mcd::Params& q, double t) {
            q.learning = t < 0.9;
            q.useLearned = t >= 0.9;
        });
        std::printf ("      learned profile: noise -%.1f dB in gaps, phrase %+.2f dB\n", gapReduction (y), toneChange (y));
        check (gapReduction (y) > 20.0, "learned profile reduces noise by more than 20 dB");
        check (std::abs (toneChange (y)) < 1.0, "phrase level kept within 1 dB");
    }

    // 5. Bands: only the top three bands reduce -> highs drop, lows stay.
    {
        mcd::Params p;
        p.reductionDb = 30.0;
        p.bandAmount = { 0, 0, 0, 0, 0, 100, 100, 100 };
        auto y = run (mix, sr, p);
        const double lowIn = bandPower (mix[0], g0, g1, sr, 80, 300), lowOut = bandPower (y[0], g0 + L, g1 + L, sr, 80, 300);
        const double hiIn = bandPower (mix[0], g0, g1, sr, 8000, 20000), hiOut = bandPower (y[0], g0 + L, g1 + L, sr, 8000, 20000);
        const double lowDb = 10.0 * std::log10 (lowIn / lowOut), hiDb = 10.0 * std::log10 (hiIn / hiOut);
        std::printf ("      bands 6-8 only: 80-300 Hz -%.1f dB, 8-20 kHz -%.1f dB\n", lowDb, hiDb);
        check (lowDb < 1.0, "bands set to 0% leave their noise alone");
        check (hiDb > 20.0, "bands set to 100% reduce their noise");
    }

    // 6. Noise-only listen mode: denoised + removed = original.
    {
        mcd::Params p;
        auto wet = run (mix, sr, p);
        p.noiseOnly = true;
        auto removed = run (mix, sr, p);
        double maxErr = 0.0;
        for (size_t i = L; i < len; ++i)
            maxErr = std::max (maxErr, (double) std::abs (wet[0][i] + removed[0][i] - mix[0][i - L]));
        check (maxErr < 1e-5, "removed-noise output + denoised output = original");
    }

    // 7. Mono and 44.1 kHz stay finite.
    {
        auto y = run (mix, 44100.0, mcd::Params {}, [] (mcd::Params&, double) {}, 1);
        bool finite = true;
        for (float v : y[0])
            finite = finite && std::isfinite (v);
        check (finite, "mono at 44.1 kHz output is finite");
    }

    // 8. Profile round trip (what the plugin saves in the Logic project).
    {
        static mcd::Denoiser d;
        d.prepare (sr, 2);
        mcd::Params p;
        p.learning = true;
        d.setParams (p);
        std::vector<float> l (mix[0].begin(), mix[0].begin() + (long) sr / 2), r = l;
        float* ptrs[2] = { l.data(), r.data() };
        d.process (ptrs, (int) l.size());
        p.learning = false;
        d.setParams (p);
        std::vector<float> l2 (2048, 0.0f), r2 = l2;
        float* ptrs2[2] = { l2.data(), r2.data() };
        d.process (ptrs2, 2048);
        auto saved = d.getLearnedProfile();
        static mcd::Denoiser d2;
        d2.prepare (sr, 2);
        d2.setLearnedProfile (saved);
        check (! saved.empty() && d2.hasLearnedProfile() && d2.getLearnedProfile() == saved, "learned profile survives save/restore");
    }

    std::printf (failures == 0 ? "\nAll tests passed\n" : "\n%d test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
