/**
 * MajorCircle Denoise v2 — streaming spectral noise reduction.
 *
 * 2048/512 Hann STFT (same framing as the AI Mastering Engine's stft.js), with:
 *  - Noise estimate: either Auto (a low percentile of each bin's magnitude over
 *    the last HISTORY_SECONDS, bias-corrected to the noise's mean power) or a
 *    Learned profile captured from a noise-only passage.
 *  - Gain rule: Wiener gain from a decision-directed a-priori SNR
 *    (Ephraim & Malah), which keeps "musical noise" much lower than a hard gate.
 *  - Gain smoothing across neighbouring bins and over time (fast open, slow close).
 *  - Per-band reduction depth (NUM_BANDS bands, log-frequency interpolated).
 *  - Gains are linked across channels (max per bin) so the stereo image holds.
 *
 * v1 ported denoise.js exactly; its floor (15-32th percentile magnitude used
 * directly as the gate threshold) sits well below the noise's typical level,
 * so most noise passed. The bias correction below is what fixes that.
 *
 * No JUCE dependency, so it can be unit-tested on any machine.
 */
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace mcd
{

constexpr int FRAME_SIZE = 2048;
constexpr int HOP_SIZE = 512; // 75% overlap
constexpr int NUM_BINS = FRAME_SIZE / 2 + 1;
constexpr int MAX_CHANNELS = 2;
constexpr int NUM_BANDS = 8;
constexpr double BAND_FREQS[NUM_BANDS] = { 60.0, 150.0, 400.0, 1000.0, 2500.0, 5000.0, 10000.0, 16000.0 };

constexpr double HISTORY_SECONDS = 8.0;
constexpr int HISTORY_DECIMATION = 2;       // store every 2nd frame in the history
constexpr int FLOOR_UPDATE_FRAMES = 8;      // every bin's floor is refreshed once per 8 frames (~85 ms)
constexpr double MIN_HISTORY_SECONDS = 0.5; // Auto passes audio through until this much has been heard
constexpr double MIN_LEARN_SECONDS = 0.25;  // a learned profile needs at least this much audio

// For Gaussian noise each bin's magnitude is Rayleigh distributed, so its p-th
// percentile m relates to the mean power by  E|X|^2 = m^2 / -ln(1 - p).
constexpr double AUTO_PERCENTILE = 20.0;
const double PERCENTILE_TO_POWER = 1.0 / -std::log (1.0 - AUTO_PERCENTILE / 100.0);

/** Iterative radix-2 Cooley-Tukey FFT, in place (port of fft.js, with tables). */
class FFT
{
public:
    explicit FFT (int n) : size (n), cosTable ((size_t) n / 2), sinTable ((size_t) n / 2), rev ((size_t) n)
    {
        for (int k = 0; k < n / 2; ++k)
        {
            cosTable[(size_t) k] = std::cos (-2.0 * M_PI * k / n);
            sinTable[(size_t) k] = std::sin (-2.0 * M_PI * k / n);
        }
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            rev[(size_t) i] = j;
        }
    }

    void forward (double* re, double* im) const
    {
        for (int i = 1; i < size; ++i)
            if (i < rev[(size_t) i])
            {
                std::swap (re[i], re[rev[(size_t) i]]);
                std::swap (im[i], im[rev[(size_t) i]]);
            }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size / len;
            for (int start = 0; start < size; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const int a = start + k, b = a + half;
                    const double wr = cosTable[(size_t) (k * step)], wi = sinTable[(size_t) (k * step)];
                    const double vr = re[b] * wr - im[b] * wi;
                    const double vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr;
                    im[b] = im[a] - vi;
                    re[a] += vr;
                    im[a] += vi;
                }
        }
    }

    /** Inverse FFT, in place (includes 1/N scaling). */
    void inverse (double* re, double* im) const
    {
        for (int i = 0; i < size; ++i)
            im[i] = -im[i];
        forward (re, im);
        for (int i = 0; i < size; ++i)
        {
            re[i] /= size;
            im[i] = -im[i] / size;
        }
    }

private:
    int size;
    std::vector<double> cosTable, sinTable;
    std::vector<int> rev;
};

struct Params
{
    double thresholdDb = 3.0;  // how far above the noise estimate a bin must be to count as signal
    double reductionDb = 20.0; // maximum attenuation of noise
    double smoothing = 50.0;   // 0..100: higher = fewer artefacts, slower response
    std::array<double, NUM_BANDS> bandAmount { 100, 100, 100, 100, 100, 100, 100, 100 }; // % of reductionDb
    bool useLearned = false;
    bool learning = false;
    bool noiseOnly = false; // output the removed part instead
};

/** Spectrum snapshot for the editor (powers, averaged over channels). */
struct Analysis
{
    std::array<float, NUM_BINS> input {}, output {}, threshold {};
    bool hasThreshold = false;
};

/** Up to MAX_CHANNELS of linked streaming denoising. Latency is FRAME_SIZE samples. */
class Denoiser
{
public:
    Denoiser() : fft (FRAME_SIZE) {}

    void prepare (double newSampleRate, int newNumChannels)
    {
        sampleRate = newSampleRate;
        numChannels = std::clamp (newNumChannels, 1, MAX_CHANNELS);

        // Periodic Hann: with 75% overlap the squared windows sum to exactly 1.5.
        window.resize (FRAME_SIZE);
        for (int i = 0; i < FRAME_SIZE; ++i)
            window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / FRAME_SIZE);

        const double framesPerSecond = sampleRate / HOP_SIZE;
        historyCapacity = std::max (16, (int) std::lround (HISTORY_SECONDS * framesPerSecond / HISTORY_DECIMATION));
        minHistory = std::max (4, (int) std::lround (MIN_HISTORY_SECONDS * framesPerSecond / HISTORY_DECIMATION));
        minLearnFrames = std::max (4, (int) std::lround (MIN_LEARN_SECONDS * framesPerSecond));

        // Each bin's position on the band axis (0 .. NUM_BANDS-1), interpolated in log frequency.
        for (int b = 0; b < NUM_BINS; ++b)
        {
            const double f = b * sampleRate / FRAME_SIZE;
            double pos = 0.0;
            if (f >= BAND_FREQS[NUM_BANDS - 1])
                pos = NUM_BANDS - 1;
            else if (f > BAND_FREQS[0])
                for (int k = 0; k < NUM_BANDS - 1; ++k)
                    if (f < BAND_FREQS[k + 1])
                    {
                        pos = k + std::log (f / BAND_FREQS[k]) / std::log (BAND_FREQS[k + 1] / BAND_FREQS[k]);
                        break;
                    }
            bandPos[(size_t) b] = pos;
        }

        for (auto& c : channels)
        {
            c.history.assign ((size_t) historyCapacity * NUM_BINS, 0.0f);
            c.scratch.resize ((size_t) historyCapacity);
        }
        reset();
    }

    void reset()
    {
        for (auto& c : channels)
        {
            c.inBuf.fill (0.0f);
            c.outBuf.fill (0.0f);
            c.dryBuf.fill (0.0f);
            c.autoNoise.fill (0.0);
            c.prevCleanPower.fill (0.0);
            c.learnSum.fill (0.0);
            c.historyWrite = 0;
            c.historyFilled = 0;
        }
        smoothedGain.fill (1.0);
        hopCount = 0;
        decimationCounter = 0;
        floorSlice = 0;
        autoReady = false;
        learnFrames = 0;
        wasLearning = false;
    }

    void setParams (const Params& p) { params = p; }

    /** Processes up to MAX_CHANNELS channels in place. */
    void process (float* const* samples, int numSamples)
    {
        const int nch = numChannels;
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < nch; ++ch)
            {
                auto& c = channels[(size_t) ch];
                c.inBuf[(size_t) (FRAME_SIZE - HOP_SIZE + hopCount)] = samples[ch][n];
                const float wet = c.outBuf[(size_t) hopCount];
                samples[ch][n] = params.noiseOnly ? c.dryBuf[(size_t) hopCount] - wet : wet;
            }

            if (++hopCount == HOP_SIZE)
            {
                hopCount = 0;
                processFrame();
            }
        }
    }

    bool hasLearnedProfile() const { return learnedReady.load(); }

    /** Learned noise power per bin, per channel (empty if none). Message thread. */
    std::vector<float> getLearnedProfile() const
    {
        std::lock_guard<std::mutex> lock (profileMutex);
        if (! learnedReady.load())
            return {};
        std::vector<float> out ((size_t) MAX_CHANNELS * NUM_BINS);
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
            for (int b = 0; b < NUM_BINS; ++b)
                out[(size_t) (ch * NUM_BINS + b)] = (float) learnedProfile[(size_t) ch][(size_t) b];
        return out;
    }

    /** Restores a profile from getLearnedProfile() (e.g. from a saved project). Message thread. */
    void setLearnedProfile (const std::vector<float>& data)
    {
        std::lock_guard<std::mutex> lock (profileMutex);
        if (data.size() != (size_t) MAX_CHANNELS * NUM_BINS)
        {
            learnedReady = false;
            return;
        }
        for (int ch = 0; ch < MAX_CHANNELS; ++ch)
            for (int b = 0; b < NUM_BINS; ++b)
                learnedProfile[(size_t) ch][(size_t) b] = data[(size_t) (ch * NUM_BINS + b)];
        learnedReady = true;
    }

    void clearLearnedProfile()
    {
        std::lock_guard<std::mutex> lock (profileMutex);
        learnedReady = false;
    }

    /** Copies the latest spectrum snapshot. Returns false if the audio thread held it. */
    bool getAnalysis (Analysis& dest)
    {
        std::unique_lock<std::mutex> lock (analysisMutex, std::try_to_lock);
        if (! lock.owns_lock())
            return false;
        dest = analysis;
        return true;
    }

private:
    struct Channel
    {
        std::array<float, FRAME_SIZE> inBuf {}, outBuf {};
        std::array<float, HOP_SIZE> dryBuf {};
        std::array<double, FRAME_SIZE> re {}, im {};
        std::array<double, NUM_BINS> power {}, autoNoise {}, prevCleanPower {}, learnSum {};
        std::vector<float> history, scratch;
        int historyWrite = 0, historyFilled = 0;
    };

    void processFrame()
    {
        const int nch = numChannels;

        for (int ch = 0; ch < nch; ++ch)
        {
            auto& c = channels[(size_t) ch];
            for (int i = 0; i < FRAME_SIZE; ++i)
            {
                c.re[(size_t) i] = c.inBuf[(size_t) i] * window[(size_t) i];
                c.im[(size_t) i] = 0.0;
            }
            fft.forward (c.re.data(), c.im.data());
            for (int b = 0; b < NUM_BINS; ++b)
                c.power[(size_t) b] = c.re[(size_t) b] * c.re[(size_t) b] + c.im[(size_t) b] * c.im[(size_t) b];
        }

        updateAutoNoise();
        updateLearning();

        // Choose the noise estimate for this frame.
        const double* noise[MAX_CHANNELS] = {};
        bool haveNoise = false;
        std::unique_lock<std::mutex> profileLock (profileMutex, std::try_to_lock);
        if (params.useLearned && profileLock.owns_lock() && learnedReady.load())
        {
            for (int ch = 0; ch < nch; ++ch)
                noise[ch] = learnedProfile[(size_t) ch].data();
            haveNoise = true;
        }
        else if (autoReady)
        {
            for (int ch = 0; ch < nch; ++ch)
                noise[ch] = channels[(size_t) ch].autoNoise.data();
            haveNoise = true;
        }

        const double smooth = std::clamp (params.smoothing, 0.0, 100.0) / 100.0;
        const double ddAlpha = 0.90 + 0.08 * smooth;  // decision-directed weight
        const double release = 0.85 * smooth;         // per-frame close-down smoothing
        const int freqRadius = (int) std::lround (3.0 * smooth);
        const double thresholdPow = std::pow (10.0, params.thresholdDb / 10.0);

        if (haveNoise)
        {
            // Wiener gain per bin, linked across channels (the most open channel wins).
            for (int b = 0; b < NUM_BINS; ++b)
            {
                double g = 0.0;
                for (int ch = 0; ch < nch; ++ch)
                {
                    auto& c = channels[(size_t) ch];
                    const double n = std::max (noise[ch][b] * thresholdPow, 1e-20);
                    const double post = c.power[(size_t) b] / n;
                    const double prior = ddAlpha * c.prevCleanPower[(size_t) b] / n + (1.0 - ddAlpha) * std::max (post - 1.0, 0.0);
                    g = std::max (g, prior / (1.0 + prior));
                }
                rawGain[(size_t) b] = g;
            }

            // Smooth across neighbouring bins.
            for (int b = 0; b < NUM_BINS; ++b)
            {
                const int lo = std::max (0, b - freqRadius), hi = std::min (NUM_BINS - 1, b + freqRadius);
                double sum = 0.0;
                for (int k = lo; k <= hi; ++k)
                    sum += rawGain[(size_t) k];
                const double g = sum / (hi - lo + 1);
                // Open immediately (keeps transients), close down smoothly (less musical noise).
                double& sg = smoothedGain[(size_t) b];
                sg = g >= sg ? g : sg * release + g * (1.0 - release);
            }

            // Per-band reduction depth.
            for (int b = 0; b < NUM_BINS; ++b)
            {
                const double pos = bandPos[(size_t) b];
                const int k = std::min ((int) pos, NUM_BANDS - 2);
                const double frac = pos - k;
                const double amount = params.bandAmount[(size_t) k] * (1.0 - frac) + params.bandAmount[(size_t) (k + 1)] * frac;
                const double depthDb = std::max (0.0, params.reductionDb) * std::clamp (amount, 0.0, 100.0) / 100.0;
                const double gMin = std::pow (10.0, -depthDb / 20.0);
                finalGain[(size_t) b] = gMin + (1.0 - gMin) * smoothedGain[(size_t) b];
            }
        }
        else
        {
            finalGain.fill (1.0);
            smoothedGain.fill (1.0);
        }

        publishAnalysis (haveNoise ? noise : nullptr, thresholdPow);
        profileLock = {};

        for (int ch = 0; ch < nch; ++ch)
        {
            auto& c = channels[(size_t) ch];
            for (int b = 0; b < NUM_BINS; ++b)
            {
                const double g = finalGain[(size_t) b];
                c.re[(size_t) b] *= g;
                c.im[(size_t) b] *= g;
                c.prevCleanPower[(size_t) b] = g * g * c.power[(size_t) b];
            }
            for (int b = NUM_BINS; b < FRAME_SIZE; ++b)
            {
                const int mirror = FRAME_SIZE - b;
                c.re[(size_t) b] = c.re[(size_t) mirror];
                c.im[(size_t) b] = -c.im[(size_t) mirror];
            }
            fft.inverse (c.re.data(), c.im.data());

            // Shift the output accumulator by one hop and overlap-add the new frame.
            std::move (c.outBuf.begin() + HOP_SIZE, c.outBuf.end(), c.outBuf.begin());
            std::fill (c.outBuf.end() - HOP_SIZE, c.outBuf.end(), 0.0f);
            constexpr double norm = 1.0 / 1.5;
            for (int i = 0; i < FRAME_SIZE; ++i)
                c.outBuf[(size_t) i] += (float) (c.re[(size_t) i] * window[(size_t) i] * norm);

            // The dry signal, delayed to line up with outBuf, for the noise-only listen mode.
            std::copy (c.inBuf.begin(), c.inBuf.begin() + HOP_SIZE, c.dryBuf.begin());
            std::move (c.inBuf.begin() + HOP_SIZE, c.inBuf.end(), c.inBuf.begin());
        }
    }

    /** Rolling per-bin percentile -> mean noise power, one slice of bins per frame. */
    void updateAutoNoise()
    {
        const bool store = ++decimationCounter >= HISTORY_DECIMATION;
        if (store)
            decimationCounter = 0;

        const int binsPerSlice = (NUM_BINS + FLOOR_UPDATE_FRAMES - 1) / FLOOR_UPDATE_FRAMES;
        const int firstBin = floorSlice * binsPerSlice;
        const int lastBin = std::min (NUM_BINS, firstBin + binsPerSlice);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];
            if (store)
            {
                float* row = c.history.data() + (size_t) c.historyWrite * NUM_BINS;
                for (int b = 0; b < NUM_BINS; ++b)
                    row[b] = (float) std::sqrt (c.power[(size_t) b]);
                c.historyWrite = (c.historyWrite + 1) % historyCapacity;
                c.historyFilled = std::min (c.historyFilled + 1, historyCapacity);
            }

            const int count = c.historyFilled;
            if (count < minHistory)
                continue;

            const double pos = (AUTO_PERCENTILE / 100.0) * (count - 1);
            const int lo = (int) std::floor (pos);
            const double frac = pos - lo;
            for (int b = firstBin; b < lastBin; ++b)
            {
                for (int f = 0; f < count; ++f)
                    c.scratch[(size_t) f] = c.history[(size_t) f * NUM_BINS + (size_t) b];
                auto first = c.scratch.begin(), last = c.scratch.begin() + count;
                std::nth_element (first, first + lo, last);
                const double vLo = c.scratch[(size_t) lo];
                const double vHi = (frac > 0.0 && lo + 1 < count) ? *std::min_element (first + lo + 1, last) : vLo;
                const double m = vLo + (vHi - vLo) * frac;
                c.autoNoise[(size_t) b] = m * m * PERCENTILE_TO_POWER;
            }
        }

        if (floorSlice == FLOOR_UPDATE_FRAMES - 1 && channels[0].historyFilled >= minHistory)
            autoReady = true;
        floorSlice = (floorSlice + 1) % FLOOR_UPDATE_FRAMES;
    }

    /** While learning, averages each bin's power; the profile is committed when learning stops. */
    void updateLearning()
    {
        if (params.learning)
        {
            if (! wasLearning)
            {
                for (auto& c : channels)
                    c.learnSum.fill (0.0);
                learnFrames = 0;
            }
            for (int ch = 0; ch < numChannels; ++ch)
                for (int b = 0; b < NUM_BINS; ++b)
                    channels[(size_t) ch].learnSum[(size_t) b] += channels[(size_t) ch].power[(size_t) b];
            ++learnFrames;
        }
        else if (wasLearning && learnFrames >= minLearnFrames)
        {
            std::unique_lock<std::mutex> lock (profileMutex, std::try_to_lock);
            if (! lock.owns_lock())
                return; // try again next frame; wasLearning stays set
            for (int ch = 0; ch < MAX_CHANNELS; ++ch)
            {
                const auto& src = channels[(size_t) std::min (ch, numChannels - 1)].learnSum;
                for (int b = 0; b < NUM_BINS; ++b)
                    learnedProfile[(size_t) ch][(size_t) b] = src[(size_t) b] / learnFrames;
            }
            learnedReady = true;
        }
        wasLearning = params.learning;
    }

    void publishAnalysis (const double* const* noise, double thresholdPow)
    {
        std::unique_lock<std::mutex> lock (analysisMutex, std::try_to_lock);
        if (! lock.owns_lock())
            return;
        const double scale = 1.0 / numChannels;
        for (int b = 0; b < NUM_BINS; ++b)
        {
            double in = 0.0, thr = 0.0;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                in += channels[(size_t) ch].power[(size_t) b];
                if (noise != nullptr)
                    thr += noise[ch][b];
            }
            const double g = finalGain[(size_t) b];
            analysis.input[(size_t) b] = (float) (in * scale);
            analysis.output[(size_t) b] = (float) (in * scale * g * g);
            analysis.threshold[(size_t) b] = (float) (thr * scale * thresholdPow);
        }
        analysis.hasThreshold = noise != nullptr;
    }

    FFT fft;
    Params params;
    double sampleRate = 48000.0;
    int numChannels = 2;
    std::vector<double> window;
    std::array<Channel, MAX_CHANNELS> channels;
    std::array<double, NUM_BINS> bandPos {}, rawGain {}, smoothedGain {}, finalGain {};
    int hopCount = 0, decimationCounter = 0, floorSlice = 0;
    int historyCapacity = 16, minHistory = 4, minLearnFrames = 4;
    bool autoReady = false;

    int learnFrames = 0;
    bool wasLearning = false;
    mutable std::mutex profileMutex;
    std::array<std::array<double, NUM_BINS>, MAX_CHANNELS> learnedProfile {};
    std::atomic<bool> learnedReady { false };

    std::mutex analysisMutex;
    Analysis analysis;
};

} // namespace mcd
