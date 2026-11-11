#include "nodes/NodeKit.h"

#include "Envelope.h"
#include "core/Prng.h"
#include "engine/Arpeggiator.h"

namespace andor::modular
{
using namespace kit;

namespace
{
    constexpr double twoPi = 6.283185307179586;

    std::vector<std::string> divisions()
    {
        std::vector<std::string> r;
        for (int i = 0; i < osp::arp::rateCount; ++i)
            r.emplace_back (osp::arp::rateName (static_cast<osp::ArpRate> (i)));
        r.emplace_back ("1 BAR");
        r.emplace_back ("2 BARS");
        r.emplace_back ("4 BARS");
        return r;
    }
    double divisionQuarters (int i) noexcept
    {
        if (i < osp::arp::rateCount)
            return osp::arp::rateQuarters (static_cast<osp::ArpRate> (std::max (0, i)));
        return i == osp::arp::rateCount ? 4.0 : (i == osp::arp::rateCount + 1 ? 8.0 : 16.0);
    }

    /** Shape value at phase (0..1), bipolar. */
    float shapeAt (int shape, double ph, float held, float smoothFrom, float smoothTo) noexcept
    {
        switch (shape)
        {
            case 0: return static_cast<float> (std::sin (twoPi * ph));
            case 1: return static_cast<float> (ph < 0.25 ? 4.0 * ph : (ph < 0.75 ? 2.0 - 4.0 * ph : 4.0 * ph - 4.0));
            case 2: return static_cast<float> (2.0 * ph - 1.0);
            case 3: return static_cast<float> (1.0 - 2.0 * ph);
            case 4: return ph < 0.5 ? 1.0f : -1.0f;
            case 5: return held;
            default:
            {
                const float t = static_cast<float> (0.5 - 0.5 * std::cos (3.14159265 * ph));
                return smoothFrom + (smoothTo - smoothFrom) * t;
            }
        }
    }

    //==========================================================================
    class Lfo final : public Processor
    {
    public:
        enum P { shape, rate, sync, division, depth, phaseOffset, retrigger, unipolar };
        void prepare (const PrepareContext& pc) override
        {
            sr = pc.sampleRate;
            seed = pc.seed;
            random.reseed (seed);
        }
        void start (const VoiceInfo& v, bool retrig) noexcept override
        {
            if (! retrig)
                return;
            random.reseed (v.seed ^ seed);
            phase = retriggerOn ? 0.0 : random.nextDouble();
            cycle = -1;
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int s = static_cast<int> (p[shape]);
            retriggerOn = p[retrigger] >= 0.5f;
            const bool synced = p[sync] >= 0.5f;
            const double quarters = divisionQuarters (static_cast<int> (p[division]));
            const float d = p[depth];
            const bool uni = p[unipolar] >= 0.5f;
            const double offset = p[phaseOffset];
            float* o = io.out[0][0];
            const double spq = sr * 60.0 / std::max (1.0, ctx.bpm);
            // GLOBAL synced LFOs follow the host's position exactly (bounces repeat); voices start
            // their own phase at the note.
            const bool followHost = synced && ctx.voice == nullptr;
            const double inc = synced ? 1.0 / (quarters * spq) : p[rate] / sr;
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                double ph;
                std::int64_t k;
                if (followHost)
                {
                    const double c = (ctx.ppq + static_cast<double> (i) / spq) / quarters + offset;
                    k = static_cast<std::int64_t> (std::floor (c));
                    ph = c - std::floor (c);
                }
                else
                {
                    phase += inc;
                    if (phase >= 1.0)
                        phase -= std::floor (phase);
                    const double c = phase + offset;
                    ph = c - std::floor (c);
                    k = cycle + (phase < lastPhase ? 1 : 0);
                    lastPhase = phase;
                }
                if (k != cycle)
                {
                    cycle = k;
                    // Random shapes: the value of cycle k is derived from the seed and k, so a
                    // synced LFO draws the same values at the same bar every time.
                    osp::Prng r (osp::Prng::deriveSeed (seed, static_cast<std::uint64_t> (k + 1000000)));
                    smoothFrom = held;
                    held = static_cast<float> (r.bipolar());
                    smoothTo = held;
                }
                float v = shapeAt (s, ph, held, smoothFrom, smoothTo);
                v = uni ? 0.5f + 0.5f * v : v;
                o[i] = v * d;
            }
            last = o[ctx.numSamples - 1];
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = last;
            t.count = 1;
        }

    private:
        double sr = 48000.0, phase = 0.0, lastPhase = 0.0;
        std::uint64_t seed = 1;
        std::int64_t cycle = -1;
        osp::Prng random { 1 };
        float held = 0.0f, smoothFrom = 0.0f, smoothTo = 0.0f, last = 0.0f;
        bool retriggerOn = false;
    };

    //==========================================================================
    class Env final : public Processor
    {
    public:
        enum P { attack, decay, sustain, release_, depth, velocity };
        void prepare (const PrepareContext& pc) override { env.setSampleRate (pc.sampleRate); }
        void reset() noexcept override { env.kill(); }
        void start (const VoiceInfo& v, bool retrigger) noexcept override
        {
            vel = v.velocity;
            if (retrigger || ! env.isActive())
                env.noteOn();
        }
        void release() noexcept override { env.noteOff(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            env.setTimes (p[attack], p[decay], p[sustain], p[release_]);
            bool now;
            if (io.inConnected[0])
                now = io.in[0][0][0] >= 0.5f;
            else if (ctx.voice != nullptr)
                now = ctx.voice->gate;
            else
            {
                for (int e = 0; e < ctx.numEvents; ++e)
                {
                    if (ctx.events[e].kind == NoteEvent::Kind::noteOn)
                    {
                        ++held;
                        env.noteOn();
                    }
                    else if (ctx.events[e].kind == NoteEvent::Kind::noteOff)
                        held = std::max (0, held - 1);
                    else
                        held = 0;
                }
                now = held > 0;
            }
            if (io.inConnected[0] || ctx.voice == nullptr)
            {
                if (now && ! gate)
                    env.noteOn();
                if (! now && gate)
                    env.noteOff();
            }
            gate = now;
            const float scale = p[depth] * (1.0f - p[velocity] + p[velocity] * (ctx.voice != nullptr ? vel : 1.0f));
            float* o = io.out[0][0];
            for (int i = 0; i < ctx.numSamples; ++i)
                o[i] = env.next() * scale;
            last = o[ctx.numSamples - 1];
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = last;
            t.values[1] = static_cast<float> (env.currentStage());
            t.count = 2;
        }

    private:
        andor::analog::Envelope env;
        float vel = 1.0f, last = 0.0f;
        int held = 0;
        bool gate = false;
    };

    //==========================================================================
    class RandomSH final : public Processor
    {
    public:
        enum P { rate, sync, division, slew, steps, distribution, depth, unipolar };
        void prepare (const PrepareContext& pc) override
        {
            sr = pc.sampleRate;
            seed = pc.seed;
        }
        void start (const VoiceInfo& v, bool) noexcept override
        {
            noteSeed = v.seed;
            draw (static_cast<std::int64_t> (counter++));
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const bool synced = p[sync] >= 0.5f;
            const double quarters = divisionQuarters (static_cast<int> (p[division]));
            const double spq = sr * 60.0 / std::max (1.0, ctx.bpm);
            const float slewCoef = p[slew] <= 0.0f ? 1.0f : static_cast<float> (1.0 - std::exp (-1.0 / (p[slew] * sr)));
            stepsNow = static_cast<int> (p[steps]);
            gaussian = static_cast<int> (p[distribution]) == 1;
            const float* trig = io.in[0][0];
            float* o = io.out[0][0];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                if (io.inConnected[0])
                {
                    if (trig[i] >= 0.5f && lastTrig < 0.5f)
                        draw (static_cast<std::int64_t> (counter++));
                    lastTrig = trig[i];
                }
                else if (synced && ctx.voice == nullptr)
                {
                    const auto k = static_cast<std::int64_t> (std::floor ((ctx.ppq + static_cast<double> (i) / spq) / quarters));
                    if (k != cycle)
                    {
                        cycle = k;
                        draw (k);   // bar-aware: the value at step k is always the same
                    }
                }
                else
                {
                    phase += synced ? 1.0 / (quarters * spq) : p[rate] / sr;
                    if (phase >= 1.0)
                    {
                        phase -= 1.0;
                        draw (static_cast<std::int64_t> (counter++));
                    }
                }
                value += slewCoef * (target - value);
                const float v = p[unipolar] >= 0.5f ? 0.5f + 0.5f * value : value;
                o[i] = v * p[depth];
            }
            last = o[ctx.numSamples - 1];
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = last;
            t.count = 1;
        }

    private:
        void draw (std::int64_t k) noexcept
        {
            osp::Prng r (osp::Prng::deriveSeed (seed ^ noteSeed, static_cast<std::uint64_t> (k + 7)));
            double x = gaussian ? std::clamp (r.gaussian() * 0.4, -1.0, 1.0) : r.bipolar();
            if (stepsNow > 0)
                x = std::round ((x * 0.5 + 0.5) * stepsNow) / stepsNow * 2.0 - 1.0;
            target = static_cast<float> (x);
        }
        double sr = 48000.0, phase = 0.0;
        std::uint64_t seed = 1, noteSeed = 0, counter = 0;
        std::int64_t cycle = -1;
        int stepsNow = 0;
        bool gaussian = false;
        float value = 0.0f, target = 0.0f, lastTrig = 0.0f, last = 0.0f;
    };

    //==========================================================================
    class Follower final : public Processor
    {
    public:
        enum P { attack, release_, gain, threshold };
        void prepare (const PrepareContext& pc) override { sr = pc.sampleRate; }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const float a = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (0.0001f, p[attack]) * sr)));
            const float r = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (0.001f, p[release_]) * sr)));
            const float g = dbToGain (p[gain]);
            const float th = p[threshold];
            float* env = io.out[0][0];
            float* trig = io.out[1][0];
            float* gate = io.out[2][0];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float x = 0.5f * (std::abs (io.in[0][0][i]) + std::abs (io.in[0][1][i])) * g;
                level += (x > level ? a : r) * (x - level);
                env[i] = std::clamp (level, 0.0f, 1.0f);
                trig[i] = 0.0f;
                if (! high && env[i] > th)
                {
                    high = true;
                    trig[i] = 1.0f;
                    ++onsets;
                }
                else if (high && env[i] < th * 0.6f)
                    high = false;
                gate[i] = high ? 1.0f : 0.0f;
            }
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = level;
            t.values[1] = static_cast<float> (onsets);
            t.count = 2;
        }

    private:
        double sr = 48000.0;
        float level = 0.0f;
        bool high = false;
        int onsets = 0;
    };

    //==========================================================================
    class NoteInfo final : public Processor
    {
    public:
        void start (const VoiceInfo& v, bool) noexcept override
        {
            osp::Prng r (v.seed);
            perNote = static_cast<float> (r.bipolar());
            fresh = true;
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const auto* v = ctx.voice;
            const float pitch = v != nullptr ? static_cast<float> ((v->pitch - 60.0) / 12.0) : 0.0f;
            const float vel = v != nullptr ? v->velocity : 0.0f;
            const float gate = v != nullptr && v->gate ? 1.0f : 0.0f;
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                io.out[0][0][i] = pitch;
                io.out[1][0][i] = vel;
                io.out[2][0][i] = gate;
                io.out[3][0][i] = (fresh && i == 0) ? 1.0f : 0.0f;
                io.out[4][0][i] = perNote;
            }
            fresh = false;
        }

    private:
        float perNote = 0.0f;
        bool fresh = false;
    };

    //==========================================================================
    class MathNode final : public Processor
    {
    public:
        enum P { op, scale, offset, curve, clampOn, steps };
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int o = static_cast<int> (p[op]);
            const bool bConnected = io.inConnected[1];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float a = io.in[0][0][i];
                const float b = bConnected ? io.in[1][0][i] : 0.0f;
                float y;
                switch (o)
                {
                    case 0: y = a + b; break;
                    case 1: y = a * (bConnected ? b : 1.0f); break;
                    case 2: y = a - b; break;
                    case 3: y = std::min (a, b); break;
                    case 4: y = std::max (a, b); break;
                    case 5: y = -a; break;
                    case 6: y = std::abs (a); break;
                    default: y = a > b ? 1.0f : 0.0f; break;   // COMPARE: a gate
                }
                switch (static_cast<int> (p[curve]))
                {
                    case 1: y = y * std::abs (y); break;                       // exponential
                    case 2: y = std::copysign (std::sqrt (std::abs (y)), y); break;   // logarithmic
                    case 3: y = std::tanh (2.0f * y); break;                  // S
                    default: break;
                }
                y = y * p[scale] + p[offset];
                if (p[steps] > 0.0f)
                    y = std::round (y * p[steps]) / p[steps];
                if (p[clampOn] >= 0.5f)
                    y = std::clamp (y, -1.0f, 1.0f);
                io.out[0][0][i] = y;
            }
        }
    };

    //==========================================================================
    class Slew final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override { sr = pc.sampleRate; }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float up = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (0.0005f, io.params[0]) * sr)));
            const float down = static_cast<float> (1.0 - std::exp (-1.0 / (std::max (0.0005f, io.params[1]) * sr)));
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float x = io.in[0][0][i];
                value += (x > value ? up : down) * (x - value);
                io.out[0][0][i] = value;
            }
        }

    private:
        double sr = 48000.0;
        float value = 0.0f;
    };
}

void registerControlNodes (Registry& r)
{
    auto controller = [] (const char* type, const char* name, const char* description, std::uint8_t domains, Domain def) {
        NodeDesc d;
        d.type = type;
        d.name = name;
        d.description = description;
        d.category = "Controllers";
        d.domains = domains;
        d.defaultDomain = def;
        d.cost.units = 0.05f;
        return d;
    };
    {
        auto d = controller ("ctl.lfo", "LFO", "Sine, triangle, saws, square, sample & hold and smooth random; free or synced", voiceOrGlobal, Domain::global);
        d.keywords = { "lfo", "modulation", "oscillator", "wobble", "vibrato", "tremolo" };
        d.help = "GLOBAL and synced: the phase is the host's position (every bounce the same). VOICE: each note starts its own (RETRIGGER: "
                 "from PHASE, else free). Its random shapes are seeded: the same cycle draws the same value.";
        d.inputs = {};
        d.outputs = { out ("out", "OUT", PortKind::control) };
        d.params = { choice ("shape", "SHAPE", { "SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "S&H", "SMOOTH" }, 0, true),
                     when (logNum ("rate", "RATE", 0.01, 40.0, 1.0, "Hz", true), "sync", { 0 }), toggle ("sync", "SYNC", false, true),
                     when (choice ("division", "DIVISION", divisions(), 6), "sync", { 1 }),
                     num ("depth", "DEPTH", 0.0, 1.0, 1.0, "%", true), num ("phase", "PHASE", 0.0, 1.0, 0.0, "%"), toggle ("retrigger", "RETRIGGER", false),
                     toggle ("unipolar", "UNIPOLAR", false) };
        d.create = make<Lfo>();
        r.add (d);
    }
    {
        auto d = controller ("ctl.env", "ENVELOPE", "An ADSR control signal: per note, from a GATE, or (global) from any held note", voiceOrGlobal, Domain::voice);
        d.keywords = { "envelope", "adsr", "contour", "eg", "modulation" };
        d.inputs = { in ("gate", "GATE", PortKind::gate), notesIn() };
        d.outputs = { out ("out", "OUT", PortKind::control, Polarity::unipolar) };
        d.params = { logNum ("attack", "ATTACK", 0.001, 10.0, 0.01, "s", true), logNum ("decay", "DECAY", 0.005, 10.0, 0.6, "s", true),
                     num ("sustain", "SUSTAIN", 0.0, 1.0, 0.3, "%", true), logNum ("release", "RELEASE", 0.005, 15.0, 0.5, "s", true),
                     num ("depth", "DEPTH", 0.0, 1.0, 1.0, "%"), num ("velocity", "VELOCITY", 0.0, 1.0, 0.3, "%") };
        d.create = make<Env>();
        r.add (d);
    }
    {
        auto d = controller ("ctl.random", "RANDOM / S&H", "Seeded random steps: on a trigger, at a rate, or on the bar; slew and steps", voiceOrGlobal, Domain::global);
        d.keywords = { "random", "sample and hold", "s&h", "chance", "variation" };
        d.inputs = { in ("trig", "TRIGGER", PortKind::trigger) };
        d.outputs = { out ("out", "OUT", PortKind::control) };
        d.params = { when (logNum ("rate", "RATE", 0.05, 30.0, 2.0, "Hz", true), "sync", { 0 }), toggle ("sync", "SYNC", false),
                     when (choice ("division", "DIVISION", divisions(), 5), "sync", { 1 }),
                     logNum ("slew", "SLEW", 0.0005, 2.0, 0.0005, "s", true), integer ("steps", "STEPS", 0, 24, 0), choice ("distribution", "SHAPE", { "EVEN", "CENTRED" }, 0),
                     num ("depth", "DEPTH", 0.0, 1.0, 1.0, "%", true), toggle ("unipolar", "UNIPOLAR", false) };
        d.create = make<RandomSH>();
        r.add (d);
    }
    {
        auto d = controller ("ctl.follower", "ENVELOPE FOLLOWER", "Any bus's level as a control, with onset trigger and gate (Rhythmic Morph anywhere)",
                             globalOnly, Domain::global);
        d.keywords = { "follower", "envelope", "sidechain", "ducking", "rhythm", "onset", "trigger" };
        d.inputs = { in ("in", "IN", PortKind::audioStereo) };
        d.outputs = { out ("env", "ENVELOPE", PortKind::control, Polarity::unipolar), out ("trig", "TRIGGER", PortKind::trigger, Polarity::unipolar),
                      out ("gate", "GATE", PortKind::gate, Polarity::unipolar) };
        d.params = { logNum ("attack", "ATTACK", 0.0005, 0.5, 0.004, "s", true), logNum ("release", "RELEASE", 0.005, 2.0, 0.15, "s", true),
                     num ("gain", "GAIN", -12.0, 36.0, 12.0, "dB", true), num ("threshold", "THRESHOLD", 0.01, 0.95, 0.3, "%") };
        d.cost.units = 0.1f;
        d.create = make<Follower>();
        r.add (d);
    }
    {
        auto d = controller ("ctl.noteinfo", "NOTE INFO", "The voice's pitch, velocity, gate, start trigger and a random value per note", voiceOnly, Domain::voice);
        d.keywords = { "note", "pitch", "velocity", "gate", "keytrack", "per note" };
        d.outputs = { out ("pitch", "PITCH", PortKind::pitch), out ("velocity", "VELOCITY", PortKind::control, Polarity::unipolar),
                      out ("gate", "GATE", PortKind::gate, Polarity::unipolar), out ("trig", "START", PortKind::trigger, Polarity::unipolar),
                      out ("random", "RANDOM", PortKind::control) };
        d.inputs = { notesIn() };
        d.create = make<NoteInfo>();
        r.add (d);
    }
    {
        auto d = controller ("ctl.math", "MATH / MAP", "Sum, multiply, invert, compare; curve, scale, offset, quantise and clamp controls", voiceOrGlobal, Domain::global);
        d.category = "Utilities";
        d.keywords = { "math", "map", "scale", "offset", "invert", "multiply", "add", "quantize", "clamp" };
        d.inputs = { in ("a", "A", PortKind::control), in ("b", "B", PortKind::control) };
        d.outputs = { out ("out", "OUT", PortKind::control) };
        d.params = { choice ("op", "OPERATION", { "A + B", "A x B", "A - B", "MIN", "MAX", "INVERT", "ABS", "A > B" }, 0, true),
                     num ("scale", "SCALE", -2.0, 2.0, 1.0, "", true), num ("offset", "OFFSET", -1.0, 1.0, 0.0, "", true),
                     choice ("curve", "CURVE", { "LINEAR", "EXP", "LOG", "S" }, 0), toggle ("clamp", "CLAMP", true), integer ("steps", "STEPS", 0, 24, 0) };
        d.create = make<MathNode>();
        r.add (d);
    }
    {
        auto d = controller ("ctl.slew", "SLEW", "Smooths a control: separate rise and fall times (portamento for controls)", voiceOrGlobal, Domain::global);
        d.category = "Utilities";
        d.keywords = { "slew", "lag", "smooth", "glide", "portamento" };
        d.inputs = { in ("in", "IN", PortKind::control) };
        d.outputs = { out ("out", "OUT", PortKind::control) };
        d.params = { logNum ("rise", "RISE", 0.0005, 5.0, 0.05, "s", true), logNum ("fall", "FALL", 0.0005, 5.0, 0.2, "s", true) };
        d.create = make<Slew>();
        r.add (d);
    }
}

} // namespace andor::modular
