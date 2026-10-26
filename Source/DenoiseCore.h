/**
 * MajorCircle Denoise — real-time port of the AI Mastering Engine's
 * stationary noise reduction (denoise.js + stft.js + fft.js).
 *
 * Same spectral gating as the web version: per-bin noise floor from a low
 * percentile of the signal's own magnitudes, soft-knee attenuation of bins at
 * or below that floor, temporal gain smoothing, 2048/512 Hann STFT.
 *
 * The one necessary difference: the web version sees the whole track at once
 * and takes the percentile over every frame. A plugin only sees audio as it
 * plays, so the floor is taken over a rolling history of the last
 * HISTORY_SECONDS instead, refreshed every FLOOR_UPDATE_FRAMES frames (an
 * eighth of the bins per frame, so the CPU cost is spread evenly).
 *
 * No JUCE dependency, so it can be unit-tested on any machine.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace mcd
{

constexpr int FRAME_SIZE = 2048;
constexpr int HOP_SIZE = 512; // 75% overlap
constexpr int NUM_BINS = FRAME_SIZE / 2 + 1;

// Same constants as denoise.js.
constexpr double MIN_PERCENTILE = 15.0, MAX_PERCENTILE = 32.0;
constexpr double MIN_KNEE_RATIO = 2.6, MAX_KNEE_RATIO = 1.5;
constexpr double TEMPORAL_SMOOTHING = 0.4;

constexpr double HISTORY_SECONDS = 8.0;
constexpr int HISTORY_DECIMATION = 2;   // store every 2nd frame in the history
constexpr int FLOOR_UPDATE_FRAMES = 8;  // every bin's floor is refreshed once per 8 frames (~85 ms)
constexpr double MIN_HISTORY_SECONDS = 0.5; // pass through until this much has been heard

/** Iterative radix-2 Cooley-Tukey FFT, in place (port of fft.js). */
class FFT
{
public:
    explicit FFT (int n) : size (n), cosTable (n / 2), sinTable (n / 2), rev (n)
    {
        for (int k = 0; k < n / 2; ++k)
        {
            cosTable[k] = std::cos (-2.0 * M_PI * k / n);
            sinTable[k] = std::sin (-2.0 * M_PI * k / n);
        }
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            rev[i] = j;
        }
    }

    void forward (double* re, double* im) const
    {
        for (int i = 1; i < size; ++i)
            if (i < rev[i])
            {
                std::swap (re[i], re[rev[i]]);
                std::swap (im[i], im[rev[i]]);
            }

        for (int len = 2; len <= size; len <<= 1)
        {
            const int half = len >> 1;
            const int step = size / len;
            for (int start = 0; start < size; start += len)
                for (int k = 0; k < half; ++k)
                {
                    const int a = start + k, b = a + half;
                    const double wr = cosTable[k * step], wi = sinTable[k * step];
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

/** One channel of streaming spectral-gate denoising. Latency is FRAME_SIZE samples. */
class ChannelDenoiser
{
public:
    ChannelDenoiser() : fft (FRAME_SIZE) {}

    void prepare (double sampleRate)
    {
        // Periodic Hann: with 75% overlap the squared windows sum to exactly 1.5,
        // so the overlap-add normalisation is a constant.
        window.resize (FRAME_SIZE);
        for (int i = 0; i < FRAME_SIZE; ++i)
            window[i] = 0.5 - 0.5 * std::cos (2.0 * M_PI * i / FRAME_SIZE);

        const double framesPerSecond = sampleRate / HOP_SIZE;
        historyCapacity = std::max (16, (int) std::lround (HISTORY_SECONDS * framesPerSecond / HISTORY_DECIMATION));
        minHistory = std::max (4, (int) std::lround (MIN_HISTORY_SECONDS * framesPerSecond / HISTORY_DECIMATION));

        history.assign ((size_t) historyCapacity * NUM_BINS, 0.0f);
        floorPerBin.assign (NUM_BINS, 0.0);
        prevGain.assign (NUM_BINS, 1.0);
        scratch.resize ((size_t) historyCapacity);
        re.assign (FRAME_SIZE, 0.0);
        im.assign (FRAME_SIZE, 0.0);
        reset();
    }

    void reset()
    {
        std::fill (inBuf.begin(), inBuf.end(), 0.0f);
        std::fill (outBuf.begin(), outBuf.end(), 0.0f);
        std::fill (dryBuf.begin(), dryBuf.end(), 0.0f);
        std::fill (prevGain.begin(), prevGain.end(), 1.0);
        std::fill (floorPerBin.begin(), floorPerBin.end(), 0.0);
        hopCount = 0;
        historyWrite = 0;
        historyFilled = 0;
        frameCounter = 0;
        floorReady = false;
    }

    /**
     * Processes samples in place.
     * strengthPercent: 0..100, same meaning as the website's 강도 slider.
     * noiseOnly: output the removed part instead (to hear what is being taken out).
     */
    void process (float* samples, int numSamples, double strengthPercent, bool noiseOnly)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            inBuf[(size_t) (FRAME_SIZE - HOP_SIZE + hopCount)] = samples[n];
            const float wet = outBuf[(size_t) hopCount];
            const float dry = dryBuf[(size_t) hopCount];
            samples[n] = noiseOnly ? dry - wet : wet;

            if (++hopCount == HOP_SIZE)
            {
                hopCount = 0;
                processFrame (strengthPercent);
            }
        }
    }

private:
    void processFrame (double strengthPercent)
    {
        for (int i = 0; i < FRAME_SIZE; ++i)
        {
            re[i] = inBuf[(size_t) i] * window[i];
            im[i] = 0.0;
        }
        fft.forward (re.data(), im.data());

        pushHistory();
        updateFloor (strengthPercent, frameCounter);
        frameCounter = (frameCounter + 1) % FLOOR_UPDATE_FRAMES;

        if (strengthPercent > 0.0 && floorReady)
        {
            const double s = std::clamp (strengthPercent, 0.0, 100.0) / 100.0;
            const double kneeRatio = MIN_KNEE_RATIO + (MAX_KNEE_RATIO - MIN_KNEE_RATIO) * s;
            const double reductionDb = 12.0 + (42.0 - 12.0) * s;
            const double reducedGain = std::pow (10.0, -reductionDb / 20.0);

            for (int b = 0; b < NUM_BINS; ++b)
            {
                const double floor = std::max (floorPerBin[(size_t) b], 1e-9);
                const double ratio = std::hypot (re[b], im[b]) / floor;
                const double t = std::clamp ((ratio - 1.0) / (kneeRatio - 1.0), 0.0, 1.0);
                const double targetGain = reducedGain + (1.0 - reducedGain) * t;
                const double smoothedGain = targetGain * (1.0 - TEMPORAL_SMOOTHING) + prevGain[(size_t) b] * TEMPORAL_SMOOTHING;
                prevGain[(size_t) b] = smoothedGain;
                re[b] *= smoothedGain;
                im[b] *= smoothedGain;
            }
        }
        else
        {
            std::fill (prevGain.begin(), prevGain.end(), 1.0);
        }

        for (int b = NUM_BINS; b < FRAME_SIZE; ++b)
        {
            const int mirror = FRAME_SIZE - b;
            re[b] = re[mirror];
            im[b] = -im[mirror];
        }
        fft.inverse (re.data(), im.data());

        // Shift the output accumulator by one hop and overlap-add the new frame.
        std::move (outBuf.begin() + HOP_SIZE, outBuf.end(), outBuf.begin());
        std::fill (outBuf.end() - HOP_SIZE, outBuf.end(), 0.0f);
        constexpr double norm = 1.0 / 1.5;
        for (int i = 0; i < FRAME_SIZE; ++i)
            outBuf[(size_t) i] += (float) (re[i] * window[i] * norm);

        // The dry signal, delayed to line up with outBuf, for the noise-only listen mode.
        std::copy (inBuf.begin(), inBuf.begin() + HOP_SIZE, dryBuf.begin());

        std::move (inBuf.begin() + HOP_SIZE, inBuf.end(), inBuf.begin());
    }

    void pushHistory()
    {
        if (++decimationCounter < HISTORY_DECIMATION)
            return;
        decimationCounter = 0;

        float* row = history.data() + (size_t) historyWrite * NUM_BINS;
        for (int b = 0; b < NUM_BINS; ++b)
            row[b] = (float) std::hypot (re[b], im[b]);

        historyWrite = (historyWrite + 1) % historyCapacity;
        historyFilled = std::min (historyFilled + 1, historyCapacity);
    }

    /** Per-bin percentile over the rolling history (utils.percentile, linear interpolation), one slice of bins. */
    void updateFloor (double strengthPercent, int slice)
    {
        if (historyFilled < minHistory)
            return;

        const double s = std::clamp (strengthPercent, 0.0, 100.0) / 100.0;
        const double p = MIN_PERCENTILE + (MAX_PERCENTILE - MIN_PERCENTILE) * s;
        const int count = historyFilled;
        const double pos = (p / 100.0) * (count - 1);
        const int lo = (int) std::floor (pos);
        const double frac = pos - lo;

        const int binsPerSlice = (NUM_BINS + FLOOR_UPDATE_FRAMES - 1) / FLOOR_UPDATE_FRAMES;
        const int firstBin = slice * binsPerSlice;
        const int lastBin = std::min (NUM_BINS, firstBin + binsPerSlice);

        for (int b = firstBin; b < lastBin; ++b)
        {
            for (int f = 0; f < count; ++f)
                scratch[(size_t) f] = history[(size_t) f * NUM_BINS + b];

            auto first = scratch.begin(), last = scratch.begin() + count;
            std::nth_element (first, first + lo, last);
            const double vLo = scratch[(size_t) lo];
            double vHi = vLo;
            if (frac > 0.0 && lo + 1 < count)
                vHi = *std::min_element (first + lo + 1, last);
            floorPerBin[(size_t) b] = vLo + (vHi - vLo) * frac;
        }
        if (slice == FLOOR_UPDATE_FRAMES - 1)
            floorReady = true;
    }

    FFT fft;
    std::vector<double> window, re, im, floorPerBin, prevGain;
    std::vector<float> inBuf = std::vector<float> (FRAME_SIZE, 0.0f);
    std::vector<float> outBuf = std::vector<float> (FRAME_SIZE, 0.0f);
    std::vector<float> dryBuf = std::vector<float> (HOP_SIZE, 0.0f);
    std::vector<float> history, scratch;
    int hopCount = 0;
    int historyCapacity = 16, historyWrite = 0, historyFilled = 0, minHistory = 4;
    int decimationCounter = 0, frameCounter = 0;
    bool floorReady = false;
};

} // namespace mcd
