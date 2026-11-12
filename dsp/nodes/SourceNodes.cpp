#include "nodes/NodeKit.h"

#include "graph/External.h"

#include "AnalogMath.h"
#include "LiveGranulator.h"
#include "OrganicVoice.h"
#include "Oscillator.h"
#include "core/Prng.h"
#include "engine/GranularSource.h"
#include "model/InstrumentModel.h"

#include <array>
#include <mutex>

namespace andor::modular
{
using namespace kit;
namespace osc = andor::analog::osc;

//==============================================================================
// External instrument factory (installed by the product or the tests).
namespace
{
    std::mutex& factoryLock()
    {
        static std::mutex m;
        return m;
    }
    ExternalFactory& factory()
    {
        static ExternalFactory f;
        return f;
    }
}

void setExternalFactory (ExternalFactory f)
{
    std::lock_guard<std::mutex> lock (factoryLock());
    factory() = std::move (f);
}

std::shared_ptr<ExternalInstrument> makeExternal (const std::string& key)
{
    std::lock_guard<std::mutex> lock (factoryLock());
    return factory() ? factory() (key) : nullptr;
}

namespace
{
    /** Pitch of a voice source: the voice's pitch (KEYTRACK), its tuning and the pitch input
        (1.0 = one octave). */
    double sourcePitch (const ChunkContext& ctx, double keytrack, double semitones, float pitchInput) noexcept
    {
        const double played = ctx.voice != nullptr ? ctx.voice->pitch : 60.0;
        return 60.0 + (played - 60.0) * keytrack + semitones + 12.0 * static_cast<double> (pitchInput);
    }

    //==========================================================================
    // ANALOG OSC: ANDOR ANALOG's oscillator models (CLASSIC / DCO / WIDE) and their B-spline
    // BLEP/BLAMP corrections, one per voice.
    class AnalogOsc final : public Processor
    {
    public:
        enum P { model, wave, dcoWave, wideWave, octave, coarse, fine, level, width, keytrack, drift, sub, spread, shape, fm, phaseReset };

        void prepare (const PrepareContext& pc) override
        {
            rate = pc.sampleRate;
            seed = pc.seed;
        }
        void reset() noexcept override
        {
            line.reset();
            extra = {};
            lastSync = 0.0f;
        }
        void start (const VoiceInfo& v, bool retrigger) noexcept override
        {
            if (! retrigger)
                return;
            random.reseed (v.seed ^ seed);
            phase = phaseResetOn ? 0.0 : random.nextDouble();
            for (auto& s : extra.sides)
                s = random.nextDouble();
            driftNow = static_cast<float> (random.bipolar());
            driftTarget = driftNow;
            driftCountdown = 0;
        }

        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int n = ctx.numSamples;
            const int m = static_cast<int> (p[model]);
            phaseResetOn = p[phaseReset] >= 0.5f;

            // Slow analog drift: a new random target every ~0.25 s, glided to.
            driftCountdown -= n;
            if (driftCountdown <= 0)
            {
                driftTarget = static_cast<float> (random.bipolar());
                driftCountdown = static_cast<int> (0.25 * rate);
            }
            driftNow += (driftTarget - driftNow) * std::min (1.0f, static_cast<float> (n) / static_cast<float> (0.25 * rate));
            const double driftCents = static_cast<double> (p[drift]) * 12.0 * static_cast<double> (driftNow);

            const double pitch = sourcePitch (ctx, p[keytrack], 12.0 * p[octave] + p[coarse] + p[fine] / 100.0 + driftCents / 100.0, io.in[2][0][0]);
            const double hz = noteToHz (pitch);
            const double baseDt = std::clamp (hz / rate, 1.0e-6, 0.45);
            lastHz = static_cast<float> (hz);

            osc::SlotPlan plan;
            plan.model = static_cast<andor::analog::OscModel> (std::clamp (m, 0, 2));
            if (m == 0)
                plan.waves = osc::wavesOf (static_cast<andor::analog::Osc2Wave> (std::clamp (static_cast<int> (p[wave]), 0, 6)));
            else if (m == 1)
            {
                andor::analog::DcoSettings d;
                const int dw = static_cast<int> (p[dcoWave]);
                d.saw = dw == 0 || dw == 2;
                d.pulse = dw == 1 || dw == 2;
                plan.waves = osc::wavesOf (d);
                plan.subLevel = std::clamp (p[sub], 0.0f, 1.0f);
            }
            else
            {
                const auto ww = static_cast<andor::analog::WideWave> (std::clamp (static_cast<int> (p[wideWave]), 0, 4));
                plan.waves = osc::wavesOf (ww);
                plan.superSaw = ww == andor::analog::WideWave::superSaw;
                plan.squareWidth = ww == andor::analog::WideWave::square;
                plan.tilt = p[shape] < 0.499f;
                if (plan.tilt)
                {
                    const double s = std::clamp (static_cast<double> (p[shape]) / 0.5, 0.0, 1.0);
                    const double fc = std::min (0.45 * rate, hz * (1.5 + 40.0 * s * s));
                    extra.tiltCoef = static_cast<float> (1.0 - std::exp (-2.0 * andor::analog::math::pi * fc / rate));
                }
                if (plan.superSaw)
                {
                    const double cents = osc::superSawCents (p[spread]);
                    for (int i = 0; i < osc::superSawSides; ++i)
                        extra.sideRatio[static_cast<std::size_t> (i)] = std::exp2 (osc::superSawOffsets[static_cast<std::size_t> (i)] * cents / 1200.0);
                    extra.sideLevel = osc::superSawSideLevel (p[spread]);
                    extra.sawNormal = osc::superSawNormal (extra.sideLevel);
                }
            }
            const float w = std::clamp (p[width], 0.02f, 0.98f);
            const float gain = levelSmooth.next (p[level], 0.01f, ctx);
            const float fmDepth = p[fm];
            const float* fmIn = io.in[0][0];
            const float* syncIn = io.in[1][0];
            const bool fmOn = io.inConnected[0] && fmDepth > 0.0f;
            const bool syncOn = io.inConnected[1];
            float* o = io.out[0][0];
            for (int i = 0; i < n; ++i)
            {
                double dt = baseDt;
                if (fmOn)
                    dt = std::clamp (dt * static_cast<double> (andor::analog::math::exp2Fast (2.0f * fmDepth * fmIn[i])), 1.0e-6, 0.45);
                double syncAt = -1.0;
                if (syncOn)
                {
                    const float s = syncIn[i];
                    if (lastSync <= 0.0f && s > 0.0f)
                        syncAt = std::clamp (static_cast<double> (s / (s - lastSync)), 0.0, 0.999);
                    lastSync = s;
                }
                double wrapAt = -1.0;
                o[i] = gain * osc::renderModel (phase, line, extra, plan, dt, w, syncAt, wrapAt);
            }
        }

        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = lastHz;
            t.values[1] = static_cast<float> (phase);
            t.count = 2;
        }

    private:
        double rate = 48000.0, phase = 0.0;
        std::uint64_t seed = 1;
        osp::Prng random { 1 };
        andor::analog::BlepLine line;
        osc::SlotExtra extra;
        Smoothed levelSmooth;
        float lastSync = 0.0f, driftNow = 0.0f, driftTarget = 0.0f, lastHz = 0.0f;
        int driftCountdown = 0;
        bool phaseResetOn = false;
    };

    //==========================================================================
    class Noise final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            rate = pc.sampleRate;
            random.reseed (pc.seed);
            global = pc.voiceIndex < 0;
        }
        void start (const VoiceInfo& v, bool) noexcept override { random.reseed (v.seed); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int color = static_cast<int> (p[0]);
            const float g = level.next (p[2], 0.01f, ctx);
            double toneHz = 30.0 * std::pow (600.0, std::clamp (static_cast<double> (p[1]), 0.0, 1.0));
            if (! global && ctx.voice != nullptr)
                toneHz *= std::exp2 (static_cast<double> (p[3]) * (ctx.voice->pitch - 60.0) / 12.0);
            const float a = static_cast<float> (1.0 - std::exp (-2.0 * 3.14159265 * std::min (toneHz, 0.45 * rate) / rate));
            const float motion = p[4];
            const int channels = global ? 2 : 1;
            for (int c = 0; c < channels; ++c)
            {
                auto& s = state[static_cast<std::size_t> (c)];
                float* o = io.out[0][static_cast<std::size_t> (c)];
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    const float white = static_cast<float> (random.bipolar());
                    float x = white;
                    if (color == 1)   // pink (Kellet's economy filter)
                    {
                        s.b0 = 0.99765f * s.b0 + white * 0.0990460f;
                        s.b1 = 0.96300f * s.b1 + white * 0.2965164f;
                        s.b2 = 0.57000f * s.b2 + white * 1.0526913f;
                        x = (s.b0 + s.b1 + s.b2 + white * 0.1848f) * 0.25f;
                    }
                    else if (color == 2)   // brown
                    {
                        s.brown = 0.995f * s.brown + white * 0.06f;
                        x = s.brown * 2.5f;
                    }
                    s.lp += a * (x - s.lp);
                    // MOTION: a slow random swell (breath, wind) on the level.
                    s.swellTarget = (i == 0 && random.nextDouble() < 0.02) ? static_cast<float> (random.nextDouble()) : s.swellTarget;
                    s.swell += 0.0002f * (s.swellTarget - s.swell);
                    o[i] = s.lp * g * (1.0f - motion + motion * 2.0f * s.swell);
                }
            }
        }

    private:
        struct State
        {
            float b0 = 0, b1 = 0, b2 = 0, brown = 0, lp = 0, swell = 0.5f, swellTarget = 0.5f;
        };
        double rate = 48000.0;
        bool global = false;
        osp::Prng random { 1 };
        std::array<State, 2> state {};
        Smoothed level;
    };

    //==========================================================================
    // ORGANIC SAMPLE: ANDOR OSP's prepared recording (root, onset, continuation loop) played
    // per voice at the note's pitch (ANDOR ANALOG's ORGANIC voice).
    class OrganicSample final : public Processor
    {
    public:
        enum P { tune, start_, loop, reverse_, root, level, keytrack };
        void prepare (const PrepareContext& pc) override
        {
            asset = pc.asset;
            rate = pc.sampleRate;
            voice.prepare (pc.sampleRate);
            // (a note starts before its first block is processed: START and REVERSE as set now)
            if (pc.params != nullptr)
            {
                startFraction = pc.params[start_];
                reverseOn = pc.params[reverse_] >= 0.5f;
            }
        }
        void reset() noexcept override { voice.stop(); }
        void start (const VoiceInfo&, bool retrigger) noexcept override
        {
            if (! retrigger && voice.playing())
                return;
            const osp::InstrumentModel* model = asset != nullptr && asset->model != nullptr ? asset->model.get() : nullptr;
            voice.start (model, startFraction, reverseOn);
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            startFraction = p[start_];
            reverseOn = p[reverse_] >= 0.5f;
            const float g = gain.next (p[level], 0.01f, ctx);
            const bool holding = ctx.voice != nullptr && ctx.voice->gate && p[loop] >= 0.5f;
            const double pitch = sourcePitch (ctx, p[keytrack], p[tune], io.in[0][0][0]);
            // ROOT auto: the analysed root when the recording has a pitch; one without (speech,
            // a texture, a drum: SampleAsset::pitched) takes C4, so middle C plays it as recorded
            // instead of octaves away from a guessed root (inaudibly low, in the reported case).
            const double autoRoot = asset != nullptr && ! asset->pitched() ? 60.0 : -1.0;
            const double step = voice.stepFor (pitch, p[root] < 0.0f ? autoRoot : static_cast<double> (p[root]));
            float* o = io.out[0][0];
            for (int i = 0; i < ctx.numSamples; ++i)
                o[i] = g * voice.next (step, holding);
            float where = -1.0f;
            playhead = voice.playhead (where) ? where : -1.0f;
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = playhead;
            t.values[1] = asset != nullptr ? static_cast<float> (asset->status) : 0.0f;
            t.count = 2;
        }

    private:
        std::shared_ptr<const SampleAsset> asset;
        andor::analog::OrganicVoice voice;
        double rate = 48000.0;
        float startFraction = 0.0f, playhead = -1.0f;
        bool reverseOn = false;
        Smoothed gain;
    };

    //==========================================================================
    // GRANULAR SOURCE: ANDOR OSP's granular engine on a recording. VOICE: a cloud per note
    // (summed to mono); GLOBAL: one stereo cloud at the last note's pitch (a drone bed).
    class Granular final : public Processor
    {
    public:
        enum P { position, size, density, tune, spread, scatter, level, pan, reverse_, keytrack };
        void prepare (const PrepareContext& pc) override
        {
            asset = pc.asset;
            rate = pc.sampleRate;
            seed = pc.seed;
            global = pc.voiceIndex < 0;
        }
        void start (const VoiceInfo& v, bool retrigger) noexcept override
        {
            if (retrigger || ! running)
                begin (v.seed);
        }
        void release() noexcept override
        {
            if (! global)
                grains.stopSpawning();
        }
        bool active() const noexcept override { return ! grains.isFinished(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            osp::GranularParams gp;
            gp.position = p[position];
            gp.sizeSeconds = p[size];
            gp.density = p[density];
            gp.tuneSemitones = 0.0;
            gp.spread = p[spread];
            gp.scatterSemitones = p[scatter];
            gp.pan = p[pan];
            gp.reverse = p[reverse_] >= 0.5f;
            params = gp;
            const float g = gain.next (p[level], 0.02f, ctx);
            const auto* model = asset != nullptr && asset->model != nullptr ? asset->model.get() : nullptr;
            if (global && ctx.events != nullptr)
                for (int e = 0; e < ctx.numEvents; ++e)
                    if (ctx.events[e].kind == NoteEvent::Kind::noteOn)
                        globalPitch = ctx.events[e].note;
            if (model == nullptr || ! model->isValid())
            {
                clear (io, 0, ctx.numSamples, global ? 2 : 1);
                return;
            }
            if (global && ! running)
                begin (seed);
            grains.setParams (gp);
            const auto& src = *model->original.source;
            const double played = global ? globalPitch : (ctx.voice != nullptr ? ctx.voice->pitch : 60.0);
            const double pitch = 60.0 + (played - 60.0) * p[keytrack] + p[tune] + 12.0 * io.in[0][0][0];
            const double grainRoot = asset->pitched() ? src.rootMidi() : 60.0;   // (as ORGANIC SAMPLE: no pitch, C4 as recorded)
            const double step = src.sampleRate() / rate * std::exp2 ((pitch - grainRoot) / 12.0);
            float* l = io.out[0][0];
            float* r = io.out[0][1];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                float a = 0.0f, b = 0.0f;
                grains.render (a, b, step);
                if (global)
                {
                    l[i] = a * g;
                    r[i] = b * g;
                }
                else
                    l[i] = 0.5f * (a + b) * g;
            }
            count = grains.grainCount();
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            std::array<osp::GranularSource::GrainView, 14> views {};
            const int n = grains.collect (views.data(), static_cast<int> (views.size()));
            t.values[0] = static_cast<float> (count);
            t.values[1] = static_cast<float> (params.position);
            for (int i = 0; i < n && i < 14; ++i)
                t.values[static_cast<std::size_t> (2 + i)] = views[static_cast<std::size_t> (i)].position;
            t.count = 2 + std::min (n, 14);
        }

    private:
        void begin (std::uint64_t s) noexcept
        {
            const auto* model = asset != nullptr && asset->model != nullptr ? asset->model.get() : nullptr;
            if (model == nullptr || ! model->isValid())
                return;
            grains.start (*model->original.source, params, rate, s);
            running = true;
        }
        std::shared_ptr<const SampleAsset> asset;
        osp::GranularSource grains;
        osp::GranularParams params;
        double rate = 48000.0, globalPitch = 60.0;
        std::uint64_t seed = 1;
        bool global = false, running = false;
        int count = 0;
        Smoothed gain;
    };

    //==========================================================================
    // RHYTHMIC / TEXTURE SOURCE (GLOBAL): a loop (a break, ticking, rain) kept on the host's
    // beat by restarting each beat's slice at its place (its pitch untouched), with its
    // level contour and onset triggers as control outputs.
    class Rhythmic final : public Processor
    {
    public:
        enum P { beats, level, sync, sensitivity, start_ };
        void prepare (const PrepareContext& pc) override
        {
            asset = pc.asset;
            rate = pc.sampleRate;
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const auto* model = asset != nullptr && asset->model != nullptr ? asset->model.get() : nullptr;
            float* l = io.out[0][0];
            float* r = io.out[0][1];
            float* env = io.out[1][0];
            float* trig = io.out[2][0];
            std::fill (trig, trig + ctx.numSamples, 0.0f);
            if (model == nullptr || ! model->isValid())
            {
                clear (io, 0, ctx.numSamples);
                std::fill (env, env + ctx.numSamples, 0.0f);
                return;
            }
            const auto& src = *model->original.source;
            const double frames = static_cast<double> (src.numFrames());
            const int loopBeats = std::max (1, static_cast<int> (p[beats] > 0.0f ? p[beats] : (asset->beats > 0.0 ? asset->beats : 4.0)));
            const double framesPerBeat = std::max (1.0, frames * (1.0 - static_cast<double> (p[start_])) / loopBeats);
            const double step = src.sampleRate() / rate;
            const float g = gain.next (p[level], 0.02f, ctx);
            const double spq = rate * 60.0 / std::max (1.0, ctx.bpm);
            const bool synced = p[sync] >= 0.5f;
            const float attack = 0.2f, releaseCoef = static_cast<float> (1.0 - std::exp (-1.0 / (0.12 * rate)));
            const float threshold = 0.05f + 0.6f * (1.0f - std::clamp (p[sensitivity], 0.0f, 1.0f));
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const double ppq = ctx.ppq + static_cast<double> (i) / spq;
                const auto beat = static_cast<std::int64_t> (std::floor (ppq));
                if (synced && beat != lastBeat)
                {
                    // A new beat: its slice starts at its place in the loop (a short crossfade).
                    lastBeat = beat;
                    fadeFrom = position;
                    fadeLeft = fadeLength;
                    position = static_cast<double> (p[start_]) * frames + static_cast<double> (((beat % loopBeats) + loopBeats) % loopBeats) * framesPerBeat
                               + (ppq - static_cast<double> (beat)) * spq * step;
                }
                float x = andor::analog::OrganicVoice::read (src, position);
                if (fadeLeft > 0)
                {
                    const float t = static_cast<float> (fadeLeft) / static_cast<float> (fadeLength);
                    x = x * (1.0f - t) + andor::analog::OrganicVoice::read (src, fadeFrom) * t;
                    fadeFrom += step;
                    --fadeLeft;
                }
                position += step;
                if (position >= frames - 2.0)
                    position = static_cast<double> (p[start_]) * frames;
                x *= static_cast<float> (src.playbackGain());
                l[i] = r[i] = x * g;
                // The envelope follower and its onset trigger.
                const float a = std::abs (x);
                follower += (a > follower ? attack : releaseCoef) * (a - follower);
                env[i] = std::clamp (follower * 3.0f, 0.0f, 1.0f);
                const bool above = env[i] > threshold;
                if (above && ! armedHigh)
                    trig[i] = 1.0f;
                armedHigh = above ? true : (env[i] < threshold * 0.6f ? false : armedHigh);
            }
            envNow = env[ctx.numSamples - 1];
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = envNow;
            t.values[1] = asset != nullptr ? static_cast<float> (asset->tempoBpm) : 0.0f;
            t.values[2] = static_cast<float> (lastBeat % 16);
            t.count = 3;
        }

    private:
        std::shared_ptr<const SampleAsset> asset;
        double rate = 48000.0, position = 0.0, fadeFrom = 0.0;
        std::int64_t lastBeat = -1;
        int fadeLeft = 0;
        static constexpr int fadeLength = 96;
        float follower = 0.0f, envNow = 0.0f;
        bool armedHigh = false;
        Smoothed gain;
    };

    //==========================================================================
    class External final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            if (pc.externals != nullptr && pc.strings != nullptr)
                host = (*pc.externals) (pc.nodeId, *pc.strings);
            else if (pc.strings != nullptr && ! pc.strings->empty() && ! (*pc.strings)[0].empty())
                host = makeExternal ((*pc.strings)[0]);
            if (host != nullptr)
                host->prepare (pc.sampleRate, maxChunk);
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            clear (io, 0, ctx.numSamples);
            if (host == nullptr || ! host->isLoaded() || ! host->claim (ctx.generation))
                return;
            std::array<NoteEvent, 256> local {};
            int count = 0;
            for (int e = 0; e < ctx.numEvents && count < 256; ++e)
            {
                local[static_cast<std::size_t> (count)] = ctx.events[e];
                local[static_cast<std::size_t> (count++)].offset = std::clamp (ctx.events[e].offset - ctx.offset, 0, ctx.numSamples - 1);
            }
            host->render (local.data(), count, io.out[0][0], io.out[0][1], ctx.numSamples);
            const float g = gain.next (dbToGain (io.params[0]), 0.02f, ctx);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < ctx.numSamples; ++i)
                    io.out[0][static_cast<std::size_t> (c)][i] *= g;
        }
        int latency() const noexcept override { return host != nullptr ? host->latencySamples() : 0; }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = host != nullptr && host->isLoaded() ? 1.0f : 0.0f;
            t.count = 1;
        }

    private:
        std::shared_ptr<ExternalInstrument> host;
        Smoothed gain;
    };

    //==========================================================================
    class LiveInput final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override { rate = pc.sampleRate; }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float g = gain.next (dbToGain (io.params[0]), 0.02f, ctx);
            const bool guard = io.params[1] >= 0.5f;
            const float hp = static_cast<float> (1.0 - std::exp (-2.0 * 3.14159265 * 40.0 / rate));
            for (int c = 0; c < 2; ++c)
            {
                const float* x = ctx.liveInput[static_cast<std::size_t> (c)];
                float* o = io.out[0][static_cast<std::size_t> (c)];
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    float v = x != nullptr ? x[i] * g : 0.0f;
                    low[static_cast<std::size_t> (c)] += hp * (v - low[static_cast<std::size_t> (c)]);
                    v -= low[static_cast<std::size_t> (c)];   // DC / rumble out
                    if (guard)
                        v = std::clamp (v, -0.5f, 0.5f);   // FEEDBACK GUARD: never louder than -6 dBFS
                    o[i] = v;
                }
            }
        }

    private:
        double rate = 48000.0;
        std::array<float, 2> low {};
        Smoothed gain;
    };
}

void registerSourceNodes (Registry& r)
{
    auto source = [] (const char* type, const char* name, const char* description) {
        NodeDesc d;
        d.type = type;
        d.name = name;
        d.description = description;
        d.category = "Sources";
        d.role = NodeRole::source;
        d.domains = voiceOnly;
        d.defaultDomain = Domain::voice;
        return d;
    };

    {
        auto d = source ("osc.analog", "ANALOG OSC", "ANDOR ANALOG's oscillator: CLASSIC VCO, DCO (with sub) and WIDE (with SUPER SAW)");
        d.keywords = { "oscillator", "vco", "dco", "saw", "pulse", "supersaw", "analog", "synth" };
        d.help = "Three models from ANDOR ANALOG with band-limited (B-spline BLEP/BLAMP) edges. FM and SYNC are audio-rate inputs: "
                 "connect another oscillator. PITCH: 1.0 = one octave.";
        d.inputs = { modIn ("fm", "FM"), modIn ("sync", "SYNC"), in ("pitch", "PITCH", PortKind::pitch), notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        d.params = { choice ("model", "MODEL", { "CLASSIC", "DCO", "WIDE" }, 0, true),
                     when (choice ("wave", "WAVE", { "SAW", "TRI", "PULSE", "SAW+TRI", "SAW+PULSE", "TRI+PULSE", "ALL" }, 0, true), "model", { 0 }),
                     when (choice ("dcoWave", "DCO WAVE", { "SAW", "PULSE", "SAW+PULSE", "SUB ONLY" }, 0, true), "model", { 1 }),
                     when (choice ("wideWave", "WIDE WAVE", { "SAW", "PULSE", "TRIANGLE", "SQUARE", "SUPER SAW" }, 0, true), "model", { 2 }),
                     integer ("octave", "OCTAVE", -3, 3, 0, "oct", true), num ("coarse", "COARSE", -12.0, 12.0, 0.0, "st"),
                     num ("fine", "FINE", -100.0, 100.0, 0.0, "ct", true), num ("level", "LEVEL", 0.0, 1.0, 0.5, "%", true),
                     num ("width", "WIDTH", 0.02, 0.98, 0.5, "%"), num ("keytrack", "KEYTRACK", 0.0, 1.0, 1.0, "%"),
                     num ("drift", "DRIFT", 0.0, 1.0, 0.15, "%"), when (num ("sub", "SUB", 0.0, 1.0, 0.0, "%"), "model", { 1 }),
                     when (num ("spread", "SPREAD", 0.0, 1.0, 0.35, "%"), "model", { 2 }), when (num ("shape", "SHAPE", 0.0, 1.0, 0.5, "%"), "model", { 2 }),
                     num ("fm", "FM DEPTH", 0.0, 1.0, 0.0, "%"), toggle ("phaseReset", "PHASE RESET", false) };
        d.cost.units = 1.0f;
        d.create = make<AnalogOsc>();
        r.add (d);
    }
    {
        auto d = source ("src.noise", "NOISE", "White, pink and brown noise with tone, key tracking and a slow natural motion");
        d.keywords = { "noise", "white", "pink", "breath", "wind", "air" };
        d.domains = voiceOrGlobal;
        d.inputs = { notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        d.params = { choice ("color", "COLOUR", { "WHITE", "PINK", "BROWN" }, 1, true), num ("tone", "TONE", 0.0, 1.0, 0.8, "%", true),
                     num ("level", "LEVEL", 0.0, 1.0, 0.4, "%", true), num ("keytrack", "KEYTRACK", 0.0, 1.0, 0.0, "%"), num ("motion", "MOTION", 0.0, 1.0, 0.0, "%") };
        d.cost.units = 0.3f;
        d.create = make<Noise>();
        r.add (d);
    }
    {
        auto d = source ("src.organic", "ORGANIC SAMPLE", "A recording as an oscillator: ANDOR OSP's root, onset and sustain loop, played per note");
        d.keywords = { "sample", "recording", "wav", "organic", "osp", "audio file" };
        d.help = "Drop a WAV, AIFF or FLAC on it (or on the canvas). It is analysed in the background: first playable (root and onset), "
                 "then sustaining (ANDOR OSP's continuation loop). A missing file keeps the module and its settings until you relink it.";
        d.inputs = { in ("pitch", "PITCH", PortKind::pitch), notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        d.params = { num ("tune", "TUNE", -24.0, 24.0, 0.0, "st", true), num ("start", "START", 0.0, 0.95, 0.0, "%", true), toggle ("loop", "LOOP", true, true),
                     toggle ("reverse", "REVERSE", false), integer ("root", "ROOT", -1, 127, -1), num ("level", "LEVEL", 0.0, 1.5, 1.0, "%", true),
                     num ("keytrack", "KEYTRACK", 0.0, 1.0, 1.0, "%") };
        d.strings = { "sample" };
        d.cost.units = 0.6f;
        d.create = make<OrganicSample>();
        r.add (d);
    }
    {
        auto d = source ("src.granular", "GRANULAR", "Grains of a recording: position, size, density, spread; live grain view");
        d.keywords = { "granular", "grains", "cloud", "texture", "key dna" };
        d.help = "ANDOR OSP's granular engine. VOICE: a cloud for each note. GLOBAL: one stereo cloud that follows the last note (a bed). "
                 "Heavy at 16 voices: lower DENSITY or polyphony if the CPU meter warns.";
        d.domains = voiceOrGlobal;
        d.inputs = { in ("pitch", "PITCH", PortKind::pitch), notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        d.params = { num ("position", "POSITION", 0.0, 1.0, 0.4, "%", true), logNum ("size", "SIZE", 0.02, 0.4, 0.12, "s", true),
                     logNum ("density", "DENSITY", 4.0, 40.0, 14.0, "/s", true), num ("tune", "TUNE", -24.0, 24.0, 0.0, "st"),
                     num ("spread", "SPREAD", 0.0, 1.0, 0.25, "%", true), num ("scatter", "SCATTER", 0.0, 12.0, 0.0, "st"),
                     num ("level", "LEVEL", 0.0, 1.5, 0.9, "%"), num ("pan", "PAN SPREAD", 0.0, 1.0, 0.4, "%"), toggle ("reverse", "REVERSE", false),
                     num ("keytrack", "KEYTRACK", 0.0, 1.0, 1.0, "%") };
        d.strings = { "sample" };
        d.cost.units = 2.5f;
        d.cost.heavy = true;
        d.endsVoice = true;
        d.create = make<Granular>();
        r.add (d);
    }
    {
        auto d = source ("src.rhythm", "RHYTHMIC SOURCE", "A loop kept on the host's beat (pitch untouched), with its envelope and onset triggers");
        d.keywords = { "loop", "break", "drum", "rhythm", "texture", "rain", "clock", "ticking" };
        d.help = "Each beat restarts its slice of the loop at its place, so the loop follows the host tempo without being re-pitched. "
                 "ENVELOPE and TRIGGER follow what you hear: patch them into a filter or an amp (Rhythmic Morph anywhere).";
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.outputs = { out ("out", "OUT", PortKind::audioStereo), out ("env", "ENVELOPE", PortKind::control, Polarity::unipolar),
                      out ("trig", "TRIGGER", PortKind::trigger, Polarity::unipolar) };
        d.params = { integer ("beats", "BEATS", 0, 32, 0, "", true), num ("level", "LEVEL", 0.0, 1.5, 0.9, "%", true), toggle ("sync", "SYNC", true, true),
                     num ("sensitivity", "SENSITIVITY", 0.0, 1.0, 0.6, "%"), num ("start", "START", 0.0, 0.9, 0.0, "%") };
        d.strings = { "sample" };
        d.cost.units = 0.5f;
        d.create = make<Rhythmic>();
        r.add (d);
    }
    {
        auto d = source ("src.external", "EXTERNAL INSTRUMENT", "One installed VST3/AU instrument as a global source (one instance for every note)");
        d.keywords = { "plugin", "vst", "vst3", "au", "kontakt", "external", "host" };
        d.help = "Hosts one instrument plugin. It is global: one instance hears every note (never copied per voice), so use GLOBAL MORPH, "
                 "filters and effects after it. A missing plugin keeps the module and its saved state silent until it is installed.";
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.inputs = { notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { num ("level", "LEVEL", -48.0, 12.0, 0.0, "dB", true) };
        d.strings = { "plugin", "state" };
        d.isExternal = true;
        d.cost.units = 4.0f;
        d.cost.heavy = true;
        d.create = make<External>();
        r.add (d);
    }
    {
        auto d = source ("src.input", "LIVE INPUT", "The host's audio input (with a feedback guard); not every DAW gives instruments one");
        d.keywords = { "input", "mic", "audio in", "live", "sidechain" };
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { num ("gain", "GAIN", -48.0, 24.0, 0.0, "dB", true), toggle ("guard", "FEEDBACK GUARD", true, true) };
        d.cost.units = 0.1f;
        d.create = make<LiveInput>();
        r.add (d);
    }
}

} // namespace andor::modular
