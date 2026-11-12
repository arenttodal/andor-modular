#include "nodes/NodeKit.h"

#include "Envelope.h"
#include "JunoChorus.h"
#include "LadderFilter.h"
#include "OtaFilter.h"
#include "morph/LiveMorph.h"

#include "core/Fft.h"
#include "core/Prng.h"
#include "engine/AbyssReverb.h"
#include "engine/DriveProcessor.h"
#include "engine/EchoDelay.h"
#include "engine/MovementBus.h"
#include "engine/SpaceReverb.h"

#include <array>
#include <complex>

namespace andor::modular
{
using namespace kit;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /** TPT state-variable filter (12 dB/oct): LP / HP / BP / notch. */
    struct Svf
    {
        float s1 = 0.0f, s2 = 0.0f;
        float process (float x, float g, float k, int mode) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v3 = x - s2;
            const float v1 = a1 * s1 + g * a1 * v3;
            const float v2 = s2 + g * v1;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            switch (mode)
            {
                case 0: return v2;                      // low
                case 1: return x - k * v1 - v2;         // high
                case 2: return v1 * k;                  // band (unity peak)
                default: return x - k * v1;             // notch
            }
        }
        void reset() noexcept { s1 = s2 = 0.0f; }
    };

    //==========================================================================
    class Filter final : public Processor
    {
    public:
        enum P { mode, cutoff, resonance, drive, keytrack, envAmount, levelComp };
        void prepare (const PrepareContext& pc) override
        {
            rate = pc.sampleRate;
            stereo = pc.voiceIndex < 0;
        }
        void reset() noexcept override
        {
            for (auto& l : ladder)
                l.reset();
            for (auto& o : ota)
                o.reset();
            for (auto& s : svf)
                s.reset();
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int m = static_cast<int> (p[mode]);
            double hz = p[cutoff];
            if (ctx.voice != nullptr)
                hz *= std::exp2 (static_cast<double> (p[keytrack]) * (ctx.voice->pitch - 60.0) / 12.0);
            if (io.inConnected[1])
                hz *= std::exp2 (7.0 * static_cast<double> (p[envAmount]) * io.in[1][0][0]);
            hz = std::clamp (hz, 16.0, 0.47 * rate);
            const float gTarget = static_cast<float> (std::tan (pi * hz / rate));
            const float gFrom = primed ? g : gTarget;
            primed = true;
            g = gTarget;
            const float res = std::clamp (p[resonance], 0.0f, 1.0f);
            const float pre = 1.0f + 3.0f * p[drive];
            const float makeup = 1.0f + (m == 0 ? static_cast<float> (p[levelComp]) * res * 2.0f : 0.0f);
            const int channels = stereo ? 2 : 1;
            for (int c = 0; c < channels; ++c)
            {
                const float* x = io.in[0][static_cast<std::size_t> (c)];
                float* o = io.out[0][static_cast<std::size_t> (c)];
                auto& lad = ladder[static_cast<std::size_t> (c)];
                auto& ot = ota[static_cast<std::size_t> (c)];
                auto& sv = svf[static_cast<std::size_t> (c)];
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    const float gi = rampAt (gFrom, gTarget, i, ctx.numSamples);
                    const float in = x[i] * pre;
                    float y;
                    switch (m)
                    {
                        case 0: y = lad.process (in, gi, 4.15f * res, 0.0f, 0.0f) * makeup; break;
                        case 1: y = ot.process (in, gi, res, andor::analog::OtaCharacter::juno, false); break;
                        case 2: y = ot.process (in, gi, res, andor::analog::OtaCharacter::jupiter, false); break;
                        case 3: y = ot.process (in, gi, res, andor::analog::OtaCharacter::jupiter, true); break;
                        default: y = sv.process (in, gi, 2.0f - 1.96f * res, m - 4); break;
                    }
                    o[i] = y / pre * (1.0f + 0.35f * p[drive]);
                }
            }
            lastHz = static_cast<float> (hz);
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = lastHz;
            t.count = 1;
        }

    private:
        double rate = 48000.0;
        bool stereo = false, primed = false;
        float g = 0.1f, lastHz = 1000.0f;
        std::array<andor::analog::LadderFilter, 2> ladder {};
        std::array<andor::analog::OtaFilter, 2> ota {};
        std::array<Svf, 2> svf {};
    };

    //==========================================================================
    /** Gate source for envelopes: the voice's key, a GATE input, or (GLOBAL) any held note. */
    struct GateTracker
    {
        int held = 0;
        bool last = false;
        /** Returns +1 on a rising gate, -1 on a falling one, 0 otherwise. */
        int update (const ChunkContext& ctx, const ProcessIO& io, int gatePort, bool voiceGate) noexcept
        {
            bool now;
            if (io.inConnected[static_cast<std::size_t> (gatePort)])
                now = io.in[static_cast<std::size_t> (gatePort)][0][0] >= 0.5f;
            else if (ctx.voice != nullptr)
                now = voiceGate;
            else
            {
                for (int e = 0; e < ctx.numEvents; ++e)
                {
                    if (ctx.events[e].kind == NoteEvent::Kind::noteOn)
                    {
                        ++held;
                        retrigger = true;
                    }
                    else if (ctx.events[e].kind == NoteEvent::Kind::noteOff)
                        held = std::max (0, held - 1);
                    else
                        held = 0;
                }
                now = held > 0;
            }
            int edge = now == last ? 0 : (now ? 1 : -1);
            if (retrigger && now && edge == 0)
                edge = 1;
            retrigger = false;
            last = now;
            return edge;
        }
        bool retrigger = false;
    };

    class AmpEnv final : public Processor
    {
    public:
        enum P { attack, decay, sustain, release_, velocity, level };
        void prepare (const PrepareContext& pc) override
        {
            env.setSampleRate (pc.sampleRate);
            stereo = pc.voiceIndex < 0;
        }
        void reset() noexcept override { env.kill(); }
        void start (const VoiceInfo& v, bool retrigger) noexcept override
        {
            velocityNow = v.velocity;
            if (retrigger || ! env.isActive())
                env.noteOn();
        }
        void release() noexcept override { env.noteOff(); }
        bool active() const noexcept override { return env.isActive(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            env.setTimes (p[attack], p[decay], p[sustain], p[release_]);
            if (ctx.voice == nullptr || io.inConnected[1])
            {
                const int edge = gate.update (ctx, io, 1, ctx.voice != nullptr && ctx.voice->gate);
                if (edge > 0)
                    env.noteOn();
                else if (edge < 0)
                    env.noteOff();
            }
            const float velGain = 1.0f - p[velocity] + p[velocity] * (ctx.voice != nullptr ? velocityNow : 1.0f);
            const float g = gain.next (p[level] * velGain, 0.005f, ctx);
            const int channels = stereo ? 2 : 1;
            float* e = io.out[1][0];
            for (int i = 0; i < ctx.numSamples; ++i)
                e[i] = env.next();
            for (int c = 0; c < channels; ++c)
            {
                const float* x = io.in[0][static_cast<std::size_t> (c)];
                float* o = io.out[0][static_cast<std::size_t> (c)];
                for (int i = 0; i < ctx.numSamples; ++i)
                    o[i] = x[i] * e[i] * g;
            }
            last = e[ctx.numSamples - 1];
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = last;
            t.values[1] = static_cast<float> (env.currentStage());
            t.count = 2;
        }

    private:
        andor::analog::Envelope env;
        GateTracker gate;
        float velocityNow = 1.0f, last = 0.0f;
        bool stereo = false;
        Smoothed gain;
    };

    //==========================================================================
    // MORPH (per voice): ANDOR ANALOG's LiveMorph between two sources playing the same note.
    struct MorphShared final : NodeShared
    {
        andor::analog::morph::LiveMorph::Shared tables;
    };

    class VoiceMorph final : public Processor
    {
    public:
        enum P { position, method, texture, attack, formants, detail, levelMatch, tuneA, tuneB, output };
        void prepare (const PrepareContext& pc) override
        {
            const auto* shared = dynamic_cast<const MorphShared*> (pc.shared);
            morph.prepare (pc.sampleRate, 1, shared != nullptr ? &shared->tables : nullptr);
            ready = shared != nullptr;
        }
        void start (const VoiceInfo& v, bool retrigger) noexcept override
        {
            if (retrigger && ready)
                morph.start (v.seed);
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            float* o = io.out[0][0];
            if (! ready)
            {
                std::fill (o, o + ctx.numSamples, 0.0f);
                return;
            }
            andor::analog::morph::LiveMorph::Control c;
            c.position = p[position];
            c.method = static_cast<andor::analog::MorphMethod> (std::clamp (static_cast<int> (p[method]), 0, 2));
            c.texture = p[texture];
            c.attack = p[attack];
            c.formants = p[formants];
            c.detail = p[detail];
            c.levelMatch = p[levelMatch] >= 0.5f;
            const double pitch = ctx.voice != nullptr ? ctx.voice->pitch : 60.0;
            c.carrierHz = noteToHz (pitch + p[tuneA]);
            c.sourceHz = noteToHz (pitch + p[tuneB]);
            morph.control (c);
            const bool layered = p[output] >= 0.5f;
            const float* a = io.in[0][0];
            const float* b = io.in[1][0];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float y = morph.process (a[i], b[i]);
                o[i] = layered ? 0.5f * (a[i] + b[i]) + morph.lastCore() : y;
            }
            core = morph.coreReady() ? 1.0f : 0.0f;
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            // The two sources' first 7 partials (dB) and whether the fusion sounds.
            for (int h = 0; h < 7; ++h)
            {
                t.values[static_cast<std::size_t> (h)] = morph.carrierPartialsDb()[static_cast<std::size_t> (h)];
                t.values[static_cast<std::size_t> (7 + h)] = morph.sourcePartialsDb()[static_cast<std::size_t> (h)];
            }
            t.values[14] = core;
            t.values[15] = static_cast<float> (morph.morphFundamental());
            t.count = 16;
        }

    private:
        andor::analog::morph::LiveMorph morph;
        bool ready = false;
        float core = 0.0f;
    };

    //==========================================================================
    // GLOBAL MORPH: two stereo buses (a polyphonic part, an external instrument) through one
    // short-time spectrum. IMPRINT gives A the spectral envelope of B (A's notes, B's colour);
    // FUSE interpolates both magnitudes in dB. Not a per-note morph: a bus has no single pitch,
    // so WEAVE and the pitch glide are per-voice only (docs/DSP_CONTRACTS.md).
    // Ends: 0 % is A and 100 % is B exactly (delayed by the frame to stay aligned).
    class GlobalMorph final : public Processor
    {
    public:
        static constexpr int order = 11, size = 1 << order, hop = size / 4;
        /** A frame's four transforms are spread over the hop that follows it (one per quarter
            hop), so no single audio block carries them all: one hop more latency. */
        static constexpr int latencyTotal = size + hop;
        enum P { position, method, detail, levelMatch, mix };

        void prepare (const PrepareContext& pc) override
        {
            rate = pc.sampleRate;
            fft = std::make_unique<osp::Fft> (order);
            window.resize (size);
            for (int i = 0; i < size; ++i)
                window[static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * pi * i / size));
            for (auto& ch : chans)
            {
                ch.inA.assign (size, 0.0f);
                ch.inB.assign (size, 0.0f);
                ch.out.assign (size, 0.0f);
                ch.delayA.assign (latencyTotal + 1, 0.0f);
                ch.delayB.assign (latencyTotal + 1, 0.0f);
                ch.work.assign (size, {});
            }
            magA.assign (size / 2 + 1, 0.0);
            magB.assign (size / 2 + 1, 0.0);
            prefixA.assign (size / 2 + 2, 0.0);
            prefixB.assign (size / 2 + 2, 0.0);
        }
        int latency() const noexcept override { return latencyTotal; }
        void reset() noexcept override
        {
            for (auto& ch : chans)
            {
                std::fill (ch.inA.begin(), ch.inA.end(), 0.0f);
                std::fill (ch.inB.begin(), ch.inB.end(), 0.0f);
                std::fill (ch.out.begin(), ch.out.end(), 0.0f);
                std::fill (ch.delayA.begin(), ch.delayA.end(), 0.0f);
                std::fill (ch.delayB.begin(), ch.delayB.end(), 0.0f);
            }
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const float pos = std::clamp (p[position], 0.0f, 1.0f);
            const float b = std::sqrt (4.0f * pos * (1.0f - pos));
            const float wA = (1.0f - pos) * (1.0f - b), wB = pos * (1.0f - b), wM = b;
            fuse = static_cast<int> (p[method]) == 1;
            posNow = pos;
            detailNow = p[detail];
            matchOn = p[levelMatch] >= 0.5f;
            const float wet = p[mix];
            const float levelCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.3 * rate)));
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto& ch = chans[static_cast<std::size_t> (c)];
                    const float a = io.in[0][static_cast<std::size_t> (c)][i];
                    const float bb = io.in[1][static_cast<std::size_t> (c)][i];
                    powA += levelCoef * (a * a - powA);
                    powB += levelCoef * (bb * bb - powB);
                    ch.inA[static_cast<std::size_t> (ch.write)] = a;
                    ch.inB[static_cast<std::size_t> (ch.write)] = bb;
                    // dry, delayed by the frame (aligned with the spectral path)
                    const auto d = static_cast<std::size_t> (ch.delayPos);
                    const float da = ch.delayA[d], db = ch.delayB[d];
                    ch.delayA[d] = a;
                    ch.delayB[d] = bb;
                    ch.delayPos = (ch.delayPos + 1) % static_cast<int> (ch.delayA.size() - 1);
                    const float m = ch.out[static_cast<std::size_t> (ch.read)];
                    ch.out[static_cast<std::size_t> (ch.read)] = 0.0f;
                    ch.read = (ch.read + 1) % size;
                    ch.write = (ch.write + 1) % size;
                    const float morphed = wA * da + wM * m + wB * (presence * db + (1.0f - presence) * da);
                    io.out[0][static_cast<std::size_t> (c)][i] = wet * morphed + (1.0f - wet) * da;
                }
                if (++sinceHop >= hop)
                {
                    sinceHop = 0;
                    // B's presence: an absent or silent B (a plugin not installed, a cloud not yet
                    // loaded) never silences A: below -66 dBFS the spectral path hands A through.
                    const float dbB = 10.0f * std::log10 (powB + 1.0e-12f);
                    presence = std::clamp ((dbB + 66.0f) / 18.0f, 0.0f, 1.0f);
                    gainA = gainB = 1.0f;
                    if (matchOn)
                    {
                        const float la = std::sqrt (powA + 1.0e-12f), lb = std::sqrt (powB + 1.0e-12f), loud = std::max (la, lb);
                        gainA = 1.0f + presence * (std::min (8.0f, loud / la) - 1.0f);
                        gainB = 1.0f + presence * (std::min (8.0f, loud / lb) - 1.0f);
                    }
                    for (int c = 0; c < 2; ++c)
                        snapshot (chans[static_cast<std::size_t> (c)]);
                }
                // The staged work of the last snapshot: L forward, L inverse, R forward, R inverse.
                if (sinceHop == 1)
                    analyse (chans[0]);
                else if (sinceHop == hop / 4)
                    synthesise (chans[0]);
                else if (sinceHop == hop / 2)
                    analyse (chans[1]);
                else if (sinceHop == 3 * hop / 4)
                    synthesise (chans[1]);
            }
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            // Eight octave bands of each side (dB), for the two-source display.
            for (int band = 0; band < 7; ++band)
            {
                t.values[static_cast<std::size_t> (band)] = bandsA[static_cast<std::size_t> (band)];
                t.values[static_cast<std::size_t> (7 + band)] = bandsB[static_cast<std::size_t> (band)];
            }
            t.values[14] = posNow;
            t.count = 15;
        }

    private:
        struct Channel
        {
            std::vector<float> inA, inB, out, delayA, delayB;
            std::vector<std::complex<double>> work;   ///< the snapshot frame, then its spectrum
            int write = 0, read = 0, delayPos = 0, olaAt = 0;
        };

        /** At the hop: the last `size` samples of both inputs, windowed, as one complex frame (A
            real, B imaginary). Its output will land one hop from now. */
        void snapshot (Channel& ch) noexcept
        {
            for (int i = 0; i < size; ++i)
            {
                const auto k = static_cast<std::size_t> ((ch.write + i) % size);   // oldest first
                const double w = window[static_cast<std::size_t> (i)];
                ch.work[static_cast<std::size_t> (i)] = { w * ch.inA[k] * gainA, w * ch.inB[k] * gainB };
            }
            ch.olaAt = (ch.read + hop) % size;
        }

        /** Forward transform and the morph of the spectra (in place in the channel's frame). */
        void analyse (Channel& ch) noexcept
        {
            auto& spectrum = ch.work;
            fft->forward (spectrum.data());
            const int half = size / 2;
            for (int k = 0; k <= half; ++k)
            {
                const auto z = spectrum[static_cast<std::size_t> (k)];
                const auto zc = std::conj (spectrum[static_cast<std::size_t> ((size - k) % size)]);
                const auto a = 0.5 * (z + zc);
                const auto bspec = std::complex<double> (0.0, -0.5) * (z - zc);
                specA[static_cast<std::size_t> (k)] = a;
                specB[static_cast<std::size_t> (k)] = bspec;
                magA[static_cast<std::size_t> (k)] = std::abs (a);
                magB[static_cast<std::size_t> (k)] = std::abs (bspec);
            }
            // Smoothed envelopes (moving average ~1/6 octave, wider with DETAIL low).
            prefixA[0] = prefixB[0] = 0.0;
            for (int k = 0; k <= half; ++k)
            {
                prefixA[static_cast<std::size_t> (k + 1)] = prefixA[static_cast<std::size_t> (k)] + magA[static_cast<std::size_t> (k)];
                prefixB[static_cast<std::size_t> (k + 1)] = prefixB[static_cast<std::size_t> (k)] + magB[static_cast<std::size_t> (k)];
            }
            const double widthFactor = 0.04 + 0.25 * (1.0 - detailNow);
            for (int k = 0; k <= half; ++k)
            {
                const int w = std::max (1, static_cast<int> (k * widthFactor));
                const int lo = std::max (0, k - w), hi = std::min (half, k + w);
                const double n = static_cast<double> (hi - lo + 1);
                const double envA = (prefixA[static_cast<std::size_t> (hi + 1)] - prefixA[static_cast<std::size_t> (lo)]) / n + 1.0e-9;
                const double envB = (prefixB[static_cast<std::size_t> (hi + 1)] - prefixB[static_cast<std::size_t> (lo)]) / n + 1.0e-9;
                std::complex<double> m;
                const auto& a = specA[static_cast<std::size_t> (k)];
                const auto& bb = specB[static_cast<std::size_t> (k)];
                if (! fuse)
                    m = a * std::min (31.6, envB / envA);   // IMPRINT: A's fine structure, B's colour (<= +30 dB)
                else
                {
                    const double ma = magA[static_cast<std::size_t> (k)] + 1.0e-12, mb = magB[static_cast<std::size_t> (k)] + 1.0e-12;
                    const double mag = std::exp ((1.0 - posNow) * std::log (ma) + posNow * std::log (mb));
                    const auto& phaseFrom = (1.0 - posNow) * ma >= posNow * mb ? a : bb;
                    m = std::polar (mag, std::arg (phaseFrom));
                }
                m = static_cast<double> (presence) * m + (1.0 - static_cast<double> (presence)) * a;
                spectrum[static_cast<std::size_t> (k)] = m;
                if (k > 0 && k < half)
                    spectrum[static_cast<std::size_t> (size - k)] = std::conj (m);
            }
            spectrum[0] = { spectrum[0].real(), 0.0 };
            spectrum[static_cast<std::size_t> (half)] = { spectrum[static_cast<std::size_t> (half)].real(), 0.0 };
            // display bands
            for (int band = 0; band < 7; ++band)
            {
                const int lo = 4 << band, hi = std::min (half, 8 << band);
                double ea = 0.0, eb = 0.0;
                for (int k = lo; k < hi; ++k)
                {
                    ea += magA[static_cast<std::size_t> (k)];
                    eb += magB[static_cast<std::size_t> (k)];
                }
                bandsA[static_cast<std::size_t> (band)] = static_cast<float> (20.0 * std::log10 (ea / (hi - lo) + 1.0e-9));
                bandsB[static_cast<std::size_t> (band)] = static_cast<float> (20.0 * std::log10 (eb / (hi - lo) + 1.0e-9));
            }
        }

        /** Inverse transform and overlap-add (Hann analysis and synthesis at 75 %: the windows sum
            to 1.5). With LEVEL MATCH both sides were brought to the louder one, so the fusion sits
            at its level. */
        void synthesise (Channel& ch) noexcept
        {
            fft->inverse (ch.work.data());
            const double norm = 1.0 / 1.5;
            for (int i = 0; i < size; ++i)
            {
                const auto k = static_cast<std::size_t> ((ch.olaAt + i) % size);
                ch.out[k] += static_cast<float> (ch.work[static_cast<std::size_t> (i)].real() * window[static_cast<std::size_t> (i)] * norm);
            }
        }

        double rate = 48000.0;
        std::unique_ptr<osp::Fft> fft;
        std::vector<float> window;
        std::array<Channel, 2> chans;
        std::array<std::complex<double>, size / 2 + 1> specA {}, specB {};
        std::vector<double> magA, magB, prefixA, prefixB;
        std::array<float, 7> bandsA {}, bandsB {};
        int sinceHop = 0;
        bool fuse = false, matchOn = true;
        float posNow = 0.5f, gainA = 1.0f, gainB = 1.0f, powA = 0.0f, powB = 0.0f, presence = 0.0f;
        double detailNow = 0.5;
    };

    //==========================================================================
    class Drive final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            drive.prepare (pc.sampleRate);
            stereo = pc.voiceIndex < 0;
        }
        void reset() noexcept override { drive.reset(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            osp::DriveProcessor::Settings s;
            s.amount = io.params[0];
            s.mode = static_cast<osp::DriveMode> (std::clamp (static_cast<int> (io.params[1]), 0, 2));
            s.tone = io.params[2];
            s.body = io.params[3];
            drive.setSettings (s);
            const float out = dbToGain (io.params[4]);
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                float l = io.in[0][0][i];
                float r = stereo ? io.in[0][1][i] : l;
                drive.process (l, r);
                io.out[0][0][i] = l * out;
                if (stereo)
                    io.out[0][1][i] = r * out;
            }
        }
        int latency() const noexcept override { return 0; }

    private:
        osp::DriveProcessor drive;
        bool stereo = false;
    };

    //==========================================================================
    /** TONE: low shelf, mid peak, high shelf and a tilt (RBJ biquads, coefficients per chunk). */
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void set (int type, double hz, double gainDb, double q, double rate) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * std::clamp (hz, 10.0, 0.45 * rate) / rate;
            const double cw = std::cos (w), sw = std::sin (w), alpha = sw / (2.0 * q);
            double B0, B1, B2, A0, A1, A2;
            if (type == 0)   // low shelf
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s);
                B1 = 2 * A * ((A - 1) - (A + 1) * cw);
                B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s;
                A1 = -2 * ((A - 1) + (A + 1) * cw);
                A2 = (A + 1) + (A - 1) * cw - s;
            }
            else if (type == 2)   // high shelf
            {
                const double s = 2.0 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s);
                B1 = -2 * A * ((A - 1) + (A + 1) * cw);
                B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s;
                A1 = 2 * ((A - 1) - (A + 1) * cw);
                A2 = (A + 1) - (A - 1) * cw - s;
            }
            else   // peak
            {
                B0 = 1 + alpha * A;
                B1 = -2 * cw;
                B2 = 1 - alpha * A;
                A0 = 1 + alpha / A;
                A1 = -2 * cw;
                A2 = 1 - alpha / A;
            }
            b0 = static_cast<float> (B0 / A0);
            b1 = static_cast<float> (B1 / A0);
            b2 = static_cast<float> (B2 / A0);
            a1 = static_cast<float> (A1 / A0);
            a2 = static_cast<float> (A2 / A0);
        }
    };

    class Tone final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            rate = pc.sampleRate;
            stereo = pc.voiceIndex < 0;
        }
        void reset() noexcept override
        {
            for (auto& ch : bands)
                for (auto& b : ch)
                    b.z1 = b.z2 = 0.0f;
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const double tilt = p[4];
            for (auto& ch : bands)
            {
                ch[0].set (0, 180.0, p[0] - tilt * 4.0, 0.7, rate);
                ch[1].set (1, p[2], p[1], 0.9, rate);
                ch[2].set (2, 5000.0, p[3] + tilt * 4.0, 0.7, rate);
            }
            const int channels = stereo ? 2 : 1;
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    float x = io.in[0][static_cast<std::size_t> (c)][i];
                    for (auto& b : bands[static_cast<std::size_t> (c)])
                        x = b.process (x);
                    io.out[0][static_cast<std::size_t> (c)][i] = x;
                }
        }

    private:
        double rate = 48000.0;
        bool stereo = false;
        std::array<std::array<Biquad, 3>, 2> bands {};
    };

    //==========================================================================
    class Space final : public Processor
    {
    public:
        enum P { type, decay, size, predelay, damping, modulation, width, lowcut, highcut, mix, gravity };
        void prepare (const PrepareContext& pc) override
        {
            rooms.prepare (pc.sampleRate);
            abyss.prepare (pc.sampleRate);
        }
        void reset() noexcept override
        {
            rooms.reset();
            abyss.reset();
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int t = static_cast<int> (p[type]);
            const bool useAbyss = t == 4;
            if (useAbyss)
            {
                osp::AbyssReverb::Settings s;
                s.decaySeconds = p[decay];
                s.size = p[size];
                s.preDelayMs = p[predelay];
                s.damping = p[damping];
                s.modulation = p[modulation];
                s.width = p[width];
                s.lowCutHz = p[lowcut];
                s.highCutHz = p[highcut];
                s.gravity = p[gravity];
                if (! configured || lastType != t || s.structurallyDifferent (abyssNow))
                    abyss.configure (s);
                else
                    abyss.tune (s);
                abyssNow = s;
            }
            else
            {
                osp::SpaceReverb::Settings s;
                s.type = static_cast<osp::SpaceType> (std::clamp (t, 0, 3));
                s.decaySeconds = p[decay];
                s.size = p[size];
                s.preDelayMs = p[predelay];
                s.damping = p[damping];
                s.modulation = p[modulation];
                s.width = p[width];
                s.lowCutHz = p[lowcut];
                s.highCutHz = p[highcut];
                if (! configured || lastType != t || s.structurallyDifferent (roomsNow))
                    rooms.configure (s);
                else
                    rooms.tune (s);
                roomsNow = s;
            }
            configured = true;
            lastType = t;
            const float wet = p[mix];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float l = io.in[0][0][i], r = io.in[0][1][i];
                float wl, wr;
                if (useAbyss)
                    abyss.process (l, r, wl, wr);
                else
                    rooms.process (l, r, wl, wr);
                io.out[0][0][i] = l * (1.0f - wet) + wl * wet;
                io.out[0][1][i] = r * (1.0f - wet) + wr * wet;
            }
        }

    private:
        osp::SpaceReverb rooms;
        osp::AbyssReverb abyss;
        osp::SpaceReverb::Settings roomsNow;
        osp::AbyssReverb::Settings abyssNow;
        bool configured = false;
        int lastType = -1;
    };

    //==========================================================================
    class Echo final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override { echo.prepare (pc.sampleRate); }
        void reset() noexcept override { echo.reset(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            osp::EchoDelay::Settings s;
            s.type = static_cast<osp::EchoType> (std::clamp (static_cast<int> (p[0]), 0, 2));
            s.sync = p[1] >= 0.5f;
            s.division = static_cast<int> (p[2]);
            s.timeMs = p[3];
            s.feedback = p[4];
            s.tone = p[5];
            s.age = p[6];
            s.stereo = static_cast<osp::EchoStereo> (std::clamp (static_cast<int> (p[7]), 0, 2));
            echo.setTempo (ctx.bpm);
            echo.setSettings (s);
            const float wet = p[8];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float l = io.in[0][0][i], r = io.in[0][1][i];
                float wl, wr;
                echo.process (l, r, wl, wr);
                io.out[0][0][i] = l + wl * wet;
                io.out[0][1][i] = r + wr * wet;
            }
        }

    private:
        osp::EchoDelay echo;
    };

    //==========================================================================
    class Movement final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override { bus.prepare (pc.sampleRate, pc.seed); }
        void reset() noexcept override { bus.reset(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            osp::Shaping s;
            s.movementMode = static_cast<osp::MovementMode> (std::clamp (static_cast<int> (p[0]), 0, 3));
            const double a = p[2], b = p[3], c = p[4];
            s.driftSpeed = a;
            s.driftPitch = b;
            s.driftTone = c;
            s.tapeWow = a;
            s.tapeFlutter = b;
            s.tapeWear = c;
            s.chorusRate = a;
            s.chorusWidth = b;
            s.chorusStereo = c;
            s.pulseRate = a;
            s.pulseShape = b;
            s.pulseStereo = c;
            osp::HostTiming t;
            t.valid = true;
            t.playing = ctx.playing;
            t.bpm = ctx.bpm;
            t.ppq = ctx.ppq;
            bus.setTiming (t);
            bus.setTargets (s, p[1]);
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                float l = io.in[0][0][i], r = io.in[0][1][i];
                bus.process (l, r);
                io.out[0][0][i] = l;
                io.out[0][1][i] = r;
            }
        }

    private:
        osp::MovementBus bus;
    };

    //==========================================================================
    class Chorus final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            chorus.prepare (pc.sampleRate, maxChunk);
            seed = static_cast<std::uint32_t> (pc.seed);
        }
        void reset() noexcept override { chorus.reset(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const int mode = static_cast<int> (io.params[0]);
            if (mode != lastMode)
            {
                chorus.setMode (mode);
                lastMode = mode;
            }
            const bool hiss = io.params[1] >= 0.5f;
            if (hiss != lastHiss)
            {
                chorus.setNoise (hiss, seed);
                lastHiss = hiss;
            }
            std::array<float, maxChunk> l {}, r {};
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                l[static_cast<std::size_t> (i)] = io.in[0][0][i];
                r[static_cast<std::size_t> (i)] = io.in[0][1][i];
            }
            chorus.process (l.data(), r.data(), ctx.numSamples);
            const float wet = io.params[2];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                io.out[0][0][i] = io.in[0][0][i] * (1.0f - wet) + l[static_cast<std::size_t> (i)] * wet;
                io.out[0][1][i] = io.in[0][1][i] * (1.0f - wet) + r[static_cast<std::size_t> (i)] * wet;
            }
        }

    private:
        andor::analog::JunoChorus chorus;
        std::uint32_t seed = 1;
        int lastMode = -1;
        bool lastHiss = false;
    };
}

void registerProcessorNodes (Registry& r)
{
    auto processor = [] (const char* type, const char* name, const char* description, std::uint8_t domains, Domain def) {
        NodeDesc d;
        d.type = type;
        d.name = name;
        d.description = description;
        d.category = "Processors";
        d.domains = domains;
        d.defaultDomain = def;
        d.inputs = { in ("in", "IN", PortKind::audioMono, true) };
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        return d;
    };

    {
        auto d = processor ("proc.filter", "FILTER", "ANDOR ANALOG's ladder and OTA (Juno / Jupiter) filters, plus 12 dB high, band and notch",
                            voiceOrGlobal, Domain::voice);
        d.keywords = { "filter", "lowpass", "highpass", "ladder", "ota", "juno", "jupiter", "vcf", "cutoff", "resonance" };
        d.help = "VOICE: one filter per note (keytracking and the envelope input per note). GLOBAL: one stereo filter after VOICE MIX. "
                 "ENV moves the cutoff by up to 7 octaves at ENV AMOUNT 100 %.";
        d.inputs.push_back (in ("env", "ENV", PortKind::control));
        d.params = { choice ("mode", "MODE", { "LADDER 24", "OTA JUNO", "OTA JUPITER", "JUPITER 12", "LOW 12", "HIGH 12", "BAND 12", "NOTCH" }, 0, true),
                     logNum ("cutoff", "CUTOFF", 20.0, 20000.0, 2400.0, "Hz", true), num ("resonance", "RESONANCE", 0.0, 1.0, 0.2, "%", true),
                     num ("drive", "DRIVE", 0.0, 1.0, 0.1, "%"), num ("keytrack", "KEYTRACK", 0.0, 1.0, 0.5, "%"),
                     num ("envAmount", "ENV AMOUNT", -1.0, 1.0, 0.3, "%", true), when (num ("comp", "BASS COMP", 0.0, 1.0, 0.5, "%"), "mode", { 0 }) };
        d.cost.units = 0.8f;
        d.create = make<Filter>();
        r.add (d);
    }
    {
        auto d = processor ("proc.vca", "AMP ENVELOPE", "The voice's articulation: an analog-shaped ADSR on the level, velocity-sensitive",
                            voiceOrGlobal, Domain::voice);
        d.keywords = { "vca", "envelope", "adsr", "amp", "articulation", "gain" };
        d.help = "Every new sound source gets one automatically. A voice lasts until its envelope has released. GATE (optional) replaces the "
                 "key; GLOBAL instances follow any held note.";
        d.inputs = { in ("in", "IN", PortKind::audioMono, true), in ("gate", "GATE", PortKind::gate), notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::audioMono), out ("env", "ENV", PortKind::control, Polarity::unipolar) };
        d.params = { logNum ("attack", "ATTACK", 0.001, 10.0, 0.005, "s", true), logNum ("decay", "DECAY", 0.005, 10.0, 0.4, "s", true),
                     num ("sustain", "SUSTAIN", 0.0, 1.0, 0.75, "%", true), logNum ("release", "RELEASE", 0.005, 15.0, 0.35, "s", true),
                     num ("velocity", "VELOCITY", 0.0, 1.0, 0.6, "%"), num ("level", "LEVEL", 0.0, 1.5, 1.0, "%") };
        d.endsVoice = true;
        d.cost.units = 0.1f;
        d.create = make<AmpEnv>();
        r.add (d);
    }
    {
        auto d = processor ("proc.morph", "MORPH", "Two sources of one note fused: IMPRINT, FUSE, WEAVE (ANDOR ANALOG's live morph)", voiceOnly, Domain::voice);
        d.keywords = { "morph", "imprint", "fuse", "weave", "spectral", "hybrid", "blend" };
        d.help = "A is the carrier, B the source; both play the same note. 0 % is A, 100 % is B, and the middle is a fusion of their "
                 "partials and noise, never a crossfade. Drop one source onto another to make one.";
        d.inputs = { in ("a", "A", PortKind::audioMono), in ("b", "B", PortKind::audioMono) };
        d.params = { num ("position", "MORPH", 0.0, 1.0, 0.5, "%", true), choice ("method", "METHOD", { "IMPRINT", "FUSE", "WEAVE" }, 1, true),
                     num ("texture", "TEXTURE", 0.0, 1.0, 0.5, "%", true), num ("attack", "ATTACK", 0.0, 1.0, 0.5, "%"),
                     num ("formants", "FORMANTS", -12.0, 12.0, 0.0, "st"), num ("detail", "DETAIL", 0.0, 1.0, 0.5, "%"),
                     toggle ("levelMatch", "LEVEL MATCH", true), num ("tuneA", "A PITCH", -24.0, 24.0, 0.0, "st"), num ("tuneB", "B PITCH", -24.0, 24.0, 0.0, "st"),
                     choice ("output", "OUTPUT", { "MORPH", "LAYERED" }, 0) };
        d.isMorph = true;
        d.cost.units = 6.0f;   // measured: tools/modular-bench (docs/PERFORMANCE_BUDGET.md)
        d.cost.heavy = true;
        d.create = make<VoiceMorph>();
        d.createShared = [] (const PrepareContext&) { return std::make_unique<MorphShared>(); };
        r.add (d);
    }
    {
        auto d = processor ("proc.morph.global", "GLOBAL MORPH", "Two stereo buses through one spectrum: IMPRINT (A's notes, B's colour) or FUSE",
                            globalOnly, Domain::global);
        d.keywords = { "morph", "imprint", "spectral", "vocoder", "global", "bus", "external", "kontakt" };
        d.help = "For buses that have no single note (a polyphonic part, an external instrument). Not a note-by-note morph: WEAVE and the "
                 "pitch glide belong to the per-voice MORPH. Adds 2560 samples of latency (a frame and a hop), reported to the host and aligned with parallel paths.";
        d.inputs = { in ("a", "A", PortKind::audioStereo), in ("b", "B", PortKind::audioStereo) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { num ("position", "MORPH", 0.0, 1.0, 0.5, "%", true), choice ("method", "METHOD", { "IMPRINT", "FUSE" }, 0, true),
                     num ("detail", "DETAIL", 0.0, 1.0, 0.5, "%", true), toggle ("levelMatch", "LEVEL MATCH", true), num ("mix", "MIX", 0.0, 1.0, 1.0, "%") };
        d.isMorph = true;
        d.latencySamples = GlobalMorph::latencyTotal;
        d.cost.units = 3.0f;
        d.cost.heavy = true;
        d.create = make<GlobalMorph>();
        r.add (d);
    }
    {
        auto d = processor ("proc.drive", "DRIVE", "ANDOR OSP's TUBE, TAPE and CRUNCH stages, oversampled", voiceOrGlobal, Domain::global);
        d.keywords = { "drive", "saturation", "distortion", "tube", "tape", "crunch", "warmth" };
        d.params = { num ("amount", "DRIVE", 0.0, 1.0, 0.3, "%", true), choice ("mode", "MODE", { "TUBE", "TAPE", "CRUNCH" }, 0, true),
                     num ("tone", "TONE", 0.0, 1.0, 0.5, "%", true), num ("body", "BODY", 0.0, 1.0, 0.5, "%"), num ("output", "OUTPUT", -24.0, 12.0, 0.0, "dB") };
        d.cost.units = 1.2f;
        d.create = make<Drive>();
        r.add (d);
    }
    {
        auto d = processor ("proc.tone", "TONE EQ", "Low shelf, mid peak, high shelf and a tilt: quick tonal shaping", voiceOrGlobal, Domain::global);
        d.keywords = { "eq", "equalizer", "tone", "tilt", "dampen", "shelf", "bass", "treble" };
        d.params = { num ("low", "LOW", -18.0, 18.0, 0.0, "dB", true), num ("mid", "MID", -18.0, 18.0, 0.0, "dB", true),
                     logNum ("midHz", "MID FREQ", 100.0, 8000.0, 900.0, "Hz"), num ("high", "HIGH", -18.0, 18.0, 0.0, "dB", true),
                     num ("tilt", "TILT", -1.0, 1.0, 0.0, "%") };
        d.cost.units = 0.3f;
        d.create = make<Tone>();
        r.add (d);
    }
    {
        auto d = processor ("fx.space", "SPACE", "ANDOR OSP's rooms (ROOM, HALL, PLATE, SPRING) and ABYSS", globalOnly, Domain::global);
        d.category = "Spatial";
        d.keywords = { "reverb", "space", "room", "hall", "plate", "spring", "abyss", "ambience" };
        d.help = "A global stereo reverb. TYPE and SIZE re-voice the room (heard at the next sound); the rest changes in place.";
        d.inputs = { in ("in", "IN", PortKind::audioStereo, true) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { choice ("type", "TYPE", { "ROOM", "HALL", "PLATE", "SPRING", "ABYSS" }, 1, true), logNum ("decay", "DECAY", 0.2, 30.0, 3.0, "s", true),
                     num ("size", "SIZE", 0.0, 1.0, 0.5, "%"), num ("predelay", "PRE-DELAY", 0.0, 250.0, 8.0, "ms"), num ("damping", "DAMPING", 0.0, 1.0, 0.4, "%"),
                     num ("modulation", "MOTION", 0.0, 1.0, 0.4, "%"), num ("width", "WIDTH", 0.0, 1.0, 1.0, "%"),
                     logNum ("lowcut", "LOW CUT", 20.0, 1000.0, 100.0, "Hz"), logNum ("highcut", "HIGH CUT", 1000.0, 20000.0, 6000.0, "Hz"),
                     num ("mix", "MIX", 0.0, 1.0, 0.3, "%", true), when (num ("gravity", "GRAVITY", -1.0, 1.0, 0.0, "%"), "type", { 4 }) };
        d.params[2].modulatable = false;   // SIZE re-voices the network
        d.cost.units = 2.0f;
        d.create = make<Space>();
        r.add (d);
    }
    {
        auto d = processor ("fx.echo", "ECHO", "ANDOR OSP's TAPE, BBD and ASCEND echoes, synced or free, feedback-safe", globalOnly, Domain::global);
        d.category = "Spatial";
        d.keywords = { "delay", "echo", "tape", "bbd", "ascend", "ping pong" };
        d.inputs = { in ("in", "IN", PortKind::audioStereo, true) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { choice ("type", "TYPE", { "TAPE", "BBD", "ASCEND" }, 0, true), toggle ("sync", "SYNC", true),
                     when (choice ("division", "DIVISION", { "1/32", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2", "1/2D", "1 BAR" }, 5), "sync", { 1 }),
                     when (logNum ("time", "TIME", 20.0, 1500.0, 375.0, "ms"), "sync", { 0 }), num ("feedback", "FEEDBACK", 0.0, 1.0, 0.45, "%", true),
                     num ("tone", "TONE", 0.0, 1.0, 0.5, "%"), num ("age", "AGE", 0.0, 1.0, 0.35, "%"),
                     choice ("stereo", "STEREO", { "MONO", "PING PONG", "WIDE" }, 1), num ("mix", "MIX", 0.0, 1.0, 0.35, "%", true) };
        d.cost.units = 1.0f;
        d.create = make<Echo>();
        r.add (d);
    }
    {
        auto d = processor ("fx.movement", "MOVEMENT", "ANDOR OSP's DRIFT, TAPE, CHORUS and PULSE motion", globalOnly, Domain::global);
        d.category = "Spatial";
        d.keywords = { "movement", "drift", "wow", "flutter", "chorus", "tremolo", "pulse" };
        d.inputs = { in ("in", "IN", PortKind::audioStereo, true) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { choice ("mode", "MODE", { "DRIFT", "TAPE", "CHORUS", "PULSE" }, 0, true), num ("amount", "AMOUNT", 0.0, 1.0, 0.4, "%", true),
                     num ("a", "RATE", 0.0, 1.0, 0.5, "%", true), num ("b", "DEPTH", 0.0, 1.0, 0.4, "%"), num ("c", "STEREO", 0.0, 1.0, 0.5, "%") };
        d.cost.units = 0.8f;
        d.create = make<Movement>();
        r.add (d);
    }
    {
        auto d = processor ("fx.chorus", "ENSEMBLE CHORUS", "ANDOR ANALOG's bucket-brigade chorus (modes I, II, I+II)", globalOnly, Domain::global);
        d.category = "Spatial";
        d.keywords = { "chorus", "ensemble", "juno", "bbd", "width" };
        d.inputs = { in ("in", "IN", PortKind::audioStereo, true) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { choice ("mode", "MODE", { "I", "II", "I+II" }, 0, true), toggle ("hiss", "HISS", false), num ("mix", "MIX", 0.0, 1.0, 1.0, "%", true) };
        d.cost.units = 0.6f;
        d.create = make<Chorus>();
        r.add (d);
    }
    {
        // REIMAGINED needs ANDOR OSP's per-recording analysis inside its sample engine; the
        // prototype does not wire it into a module yet. Listed (honestly) as unavailable.
        auto d = processor ("proc.reimagined", "REIMAGINED", "Kaleidoscope, Tape Frame, Toybox, Mirage, Mosaic (needs a recording's analysis)",
                            voiceOnly, Domain::voice);
        d.keywords = { "reimagined", "kaleidoscope", "tape frame", "toybox", "mirage", "mosaic" };
        d.available = false;
        d.unavailableReason = "not in this prototype: REIMAGINED reads ANDOR OSP's per-recording analysis, which only its sample engine "
                              "carries today (docs/RELEASE_BLOCKERS.md)";
        d.params = { choice ("mode", "MODE", { "KALEIDOSCOPE", "TAPE FRAME", "TOYBOX", "MIRAGE", "MOSAIC" }, 0, true) };
        d.cost.units = 2.0f;
        r.add (d);
    }
}

} // namespace andor::modular
