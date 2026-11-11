#include "nodes/NodeKit.h"

#include <array>

namespace andor::modular
{
using namespace kit;

namespace
{
    //==========================================================================
    /** VOICE MIX: where a voice chain becomes one stereo bus. Every voice of every lane that
        feeds it is added in (with its steal/end fade), placed by SPREAD (by pitch), PAN and its
        per-voice PAN input. */
    class VoiceMix final : public Processor
    {
    public:
        void beginChunk (int n) noexcept override
        {
            for (auto& ch : accum)
                std::fill (ch.begin(), ch.begin() + n, 0.0f);
            voices = 0;
        }
        void accumulate (const ChunkContext& ctx, const ProcessIO& io, const VoiceInfo& v) noexcept override
        {
            const float* p = io.params;
            const float spread = p[1];
            const float pitchPlace = std::clamp (static_cast<float> ((v.pitch - 60.0) / 24.0), -1.0f, 1.0f);
            const float alt = (v.voiceIndex & 1) != 0 ? 0.35f : -0.35f;
            float pan = p[2] + spread * 0.5f * (pitchPlace + alt);
            if (io.inConnected[1])
                pan += io.in[1][0][0];
            float gl, gr;
            panGains (std::clamp (pan, -1.0f, 1.0f), gl, gr);
            const float level = p[0] * 1.41421356f;
            const float* x = io.in[0][0];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float s = x[i] * level * rampAt (v.fade, v.fadeEnd, i, ctx.numSamples);
                accum[0][static_cast<std::size_t> (i)] += s * gl;
                accum[1][static_cast<std::size_t> (i)] += s * gr;
            }
            ++voices;
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            for (int c = 0; c < 2; ++c)
                std::copy (accum[static_cast<std::size_t> (c)].begin(), accum[static_cast<std::size_t> (c)].begin() + ctx.numSamples,
                           io.out[0][static_cast<std::size_t> (c)]);
            shown = voices;
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = static_cast<float> (shown);
            t.count = 1;
        }

    private:
        std::array<std::array<float, maxChunk>, 2> accum {};
        int voices = 0, shown = 0;
    };

    //==========================================================================
    class PolyToGlobal final : public Processor
    {
    public:
        void beginChunk (int) noexcept override
        {
            sum = 0.0f;
            maxv = -1.0e9f;
            count = 0;
        }
        void accumulate (const ChunkContext&, const ProcessIO& io, const VoiceInfo& v) noexcept override
        {
            const float x = io.in[0][0][0] * v.fade;
            sum += x;
            maxv = std::max (maxv, x);
            lastv = x;
            ++count;
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const int mode = static_cast<int> (io.params[0]);
            float y = 0.0f;
            if (count > 0)
                y = mode == 0 ? sum : (mode == 1 ? sum / static_cast<float> (count) : (mode == 2 ? maxv : lastv));
            std::fill (io.out[0][0], io.out[0][0] + ctx.numSamples, y);
        }

    private:
        float sum = 0.0f, maxv = 0.0f, lastv = 0.0f;
        int count = 0;
    };

    //==========================================================================
    class Mixer final : public Processor
    {
    public:
        static constexpr int inputs = 4;
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            bool anySolo = false;
            for (int k = 0; k < inputs; ++k)
                anySolo = anySolo || p[3 * inputs + k] >= 0.5f;
            for (int c = 0; c < 2; ++c)
                std::fill (io.out[0][static_cast<std::size_t> (c)], io.out[0][static_cast<std::size_t> (c)] + ctx.numSamples, 0.0f);
            const float master = dbToGain (p[4 * inputs]);
            for (int k = 0; k < inputs; ++k)
            {
                const auto ks = static_cast<std::size_t> (k);
                if (! io.inConnected[ks])
                    continue;
                const bool muted = p[2 * inputs + k] >= 0.5f || (anySolo && p[3 * inputs + k] < 0.5f);
                float gl, gr;
                panGains (p[inputs + k], gl, gr);
                const float level = muted ? 0.0f : dbToGain (p[k]) * master;
                const float tl = level * gl * 1.41421356f, tr = level * gr * 1.41421356f;
                const float fl = gains[ks][0].next (tl, 0.01f, ctx), fr = gains[ks][1].next (tr, 0.01f, ctx);
                float peak = 0.0f;
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    const float l = io.in[ks][0][i], r = io.in[ks][1][i];
                    io.out[0][0][i] += l * fl;
                    io.out[0][1][i] += r * fr;
                    peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
                }
                meters[ks] = std::max (peak, meters[ks] * 0.9f);
            }
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            for (int k = 0; k < inputs; ++k)
                t.values[static_cast<std::size_t> (k)] = meters[static_cast<std::size_t> (k)];
            t.count = inputs;
        }

    private:
        std::array<std::array<Smoothed, 2>, inputs> gains {};
        std::array<float, inputs> meters {};
    };

    //==========================================================================
    /** SWITCH: picks one of four inputs; SELECT between two of them crossfades (equal power),
        so a scene or a macro moving it never clicks. */
    class Switch final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override { stereo = pc.voiceIndex < 0; }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float target = std::clamp (io.params[0], 0.0f, 3.0f);
            const bool hard = io.params[1] >= 0.5f;
            const float sel = position.next (hard ? std::round (target) : target, hard ? 0.008f : 0.02f, ctx);
            const int a = std::clamp (static_cast<int> (std::floor (sel)), 0, 3);
            const int b = std::min (3, a + 1);
            const float t = sel - static_cast<float> (a);
            const float ga = std::cos (1.5707963f * t), gb = std::sin (1.5707963f * t);
            const int channels = stereo ? 2 : 1;
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < ctx.numSamples; ++i)
                    io.out[0][static_cast<std::size_t> (c)][i] = io.in[static_cast<std::size_t> (a)][static_cast<std::size_t> (c)][i] * ga
                                                               + io.in[static_cast<std::size_t> (b)][static_cast<std::size_t> (c)][i] * gb;
            now = sel;
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = now;
            t.count = 1;
        }

    private:
        Smoothed position;
        bool stereo = false;
        float now = 0.0f;
    };

    //==========================================================================
    class Split final : public Processor
    {
    public:
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float l = io.in[0][0][i], r = io.in[0][1][i];
                io.out[0][0][i] = l;
                io.out[1][0][i] = r;
                io.out[2][0][i] = 0.5f * (l + r);
                io.out[3][0][i] = 0.5f * (l - r);
            }
        }
    };

    class Merge final : public Processor
    {
    public:
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const bool midSide = io.params[0] >= 0.5f;
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float a = io.in[0][0][i], b = io.in[1][0][i];
                io.out[0][0][i] = midSide ? a + b : a;
                io.out[0][1][i] = midSide ? a - b : b;
            }
        }
    };

    //==========================================================================
    class Gain final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override { stereo = pc.voiceIndex < 0; }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            float target = dbToGain (io.params[0]) * (io.params[1] >= 0.5f ? -1.0f : 1.0f);
            // CV: a unipolar control multiplies the level (a VCA for followers and envelopes).
            const bool cv = io.inConnected[1];
            const float from = gain.value;
            const float g = gain.next (target, 0.005f, ctx);
            const int channels = stereo ? 2 : 1;
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    const float vca = cv ? std::clamp (io.in[1][0][i], 0.0f, 2.0f) : 1.0f;
                    io.out[0][static_cast<std::size_t> (c)][i] = io.in[0][static_cast<std::size_t> (c)][i] * rampAt (from, g, i, ctx.numSamples) * vca;
                }
        }

    private:
        Smoothed gain;
        bool stereo = false;
    };

    //==========================================================================
    /** FEEDBACK DELAY: the only module that may close a loop. Its input is read at least one
        chunk later (32 samples), its gain is held below 1 with a soft limiter, and DC is removed,
        so a loop can ring but never run away. */
    class FeedbackDelay final : public Processor
    {
    public:
        static constexpr double maxSeconds = 2.0;
        void prepare (const PrepareContext& pc) override
        {
            sr = pc.sampleRate;
            size = static_cast<int> (maxSeconds * sr) + 2 * maxChunk + 2;
            for (auto& l : lines)
                l.assign (static_cast<std::size_t> (size), 0.0f);
        }
        void reset() noexcept override
        {
            for (auto& l : lines)
                std::fill (l.begin(), l.end(), 0.0f);
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const int delay = std::clamp (static_cast<int> (io.params[0] * 0.001 * sr), maxChunk, size - maxChunk - 1);
            const float g = std::clamp (io.params[1], 0.0f, 0.95f);
            const float damp = 1.0f - 0.9f * io.params[2];
            for (int c = 0; c < 2; ++c)
            {
                auto& line = lines[static_cast<std::size_t> (c)];
                float* o = io.out[0][static_cast<std::size_t> (c)];
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    const int at = ((write + i - delay) % size + size) % size;
                    float y = line[static_cast<std::size_t> (at)] * g;
                    low[static_cast<std::size_t> (c)] += damp * (y - low[static_cast<std::size_t> (c)]);
                    y = low[static_cast<std::size_t> (c)];
                    dc[static_cast<std::size_t> (c)] += 0.0005f * (y - dc[static_cast<std::size_t> (c)]);
                    y -= dc[static_cast<std::size_t> (c)];
                    o[i] = std::tanh (y);
                }
            }
        }
        void writeFeedback (const ChunkContext& ctx, const ProcessIO& io) noexcept override
        {
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                for (int c = 0; c < 2; ++c)
                    lines[static_cast<std::size_t> (c)][static_cast<std::size_t> ((write + i) % size)] = io.in[0][static_cast<std::size_t> (c)][i];
            }
            write = (write + ctx.numSamples) % size;
        }

    private:
        double sr = 48000.0;
        int size = 1, write = 0;
        std::array<std::vector<float>, 2> lines;
        std::array<float, 2> low {}, dc {};
    };

    //==========================================================================
    class Output final : public Processor
    {
    public:
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float from = gain.value;
            const float g = gain.next (io.params[1] >= 0.5f ? 0.0f : dbToGain (io.params[0]), 0.01f, ctx);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < ctx.numSamples; ++i)
                {
                    const float y = io.in[0][static_cast<std::size_t> (c)][i] * rampAt (from, g, i, ctx.numSamples);
                    io.out[0][static_cast<std::size_t> (c)][i] = y;
                    peak[static_cast<std::size_t> (c)] = std::max (peak[static_cast<std::size_t> (c)] * 0.9995f, std::abs (y));
                }
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = peak[0];
            t.values[1] = peak[1];
            t.count = 2;
        }

    private:
        Smoothed gain;
        std::array<float, 2> peak {};
    };

    //==========================================================================
    class Scope final : public Processor
    {
    public:
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float l = io.in[0][0][i], r = io.in[0][1][i];
                io.out[0][0][i] = l;
                io.out[0][1][i] = r;
                peak = std::max (peak * 0.9997f, std::max (std::abs (l), std::abs (r)));
                rms += 0.0005f * (0.5f * (l * l + r * r) - rms);
                if ((++decimate % 64) == 0)
                {
                    ring[static_cast<std::size_t> (head)] = 0.5f * (l + r);
                    head = (head + 1) % static_cast<int> (ring.size());
                }
            }
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = peak;
            t.values[1] = std::sqrt (std::max (0.0f, rms));
            for (int i = 0; i < 14; ++i)
                t.values[static_cast<std::size_t> (2 + i)] = ring[static_cast<std::size_t> ((head + i) % static_cast<int> (ring.size()))];
            t.count = 16;
        }

    private:
        std::array<float, 14> ring {};
        int head = 0, decimate = 0;
        float peak = 0.0f, rms = 0.0f;
    };
}

void registerUtilityNodes (Registry& r)
{
    auto utility = [] (const char* type, const char* name, const char* description) {
        NodeDesc d;
        d.type = type;
        d.name = name;
        d.description = description;
        d.category = "Utilities";
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.cost.units = 0.05f;
        return d;
    };
    {
        auto d = utility ("util.voicemix", "VOICE MIX", "Where voices become one stereo bus: level, spread by pitch, pan per voice");
        d.keywords = { "voice mix", "poly", "sum", "voices", "boundary" };
        d.help = "The visible boundary between the per-note world (left) and the global one (right). Several sources may enter; the notes of "
                 "independent chains (each with its own ARP) stay independent.";
        d.role = NodeRole::boundary;
        auto inPort = in ("in", "IN", PortKind::audioMono, true);
        inPort.voiceSide = true;
        auto panPort = in ("pan", "PAN", PortKind::control);
        panPort.voiceSide = true;
        d.inputs = { inPort, panPort };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { num ("level", "LEVEL", 0.0, 1.0, 0.5, "%", true), num ("spread", "SPREAD", 0.0, 1.0, 0.3, "%", true), num ("pan", "PAN", -1.0, 1.0, 0.0, "", true) };
        d.create = make<VoiceMix>();
        r.add (d);
    }
    {
        auto d = utility ("util.polyglobal", "POLY > GLOBAL", "Per-voice controls combined into one: sum, average, highest or newest");
        d.keywords = { "poly", "global", "combine", "voices", "average" };
        d.role = NodeRole::boundary;
        auto inPort = in ("in", "IN", PortKind::control, true);
        inPort.voiceSide = true;
        d.inputs = { inPort };
        d.outputs = { out ("out", "OUT", PortKind::control) };
        d.params = { choice ("mode", "MODE", { "SUM", "AVERAGE", "HIGHEST", "NEWEST" }, 1, true) };
        d.create = make<PolyToGlobal>();
        r.add (d);
    }
    {
        auto d = utility ("util.mixer", "MIXER", "Four stereo inputs: level, pan, mute and solo, with meters");
        d.keywords = { "mixer", "mix", "sum", "level", "bus" };
        d.inputs = { in ("in1", "1", PortKind::audioStereo), in ("in2", "2", PortKind::audioStereo), in ("in3", "3", PortKind::audioStereo),
                     in ("in4", "4", PortKind::audioStereo) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        for (int k = 1; k <= 4; ++k)
            d.params.push_back (num ("level" + std::to_string (k), "LEVEL " + std::to_string (k), -60.0, 6.0, -3.0, "dB", k <= 2));
        for (int k = 1; k <= 4; ++k)
            d.params.push_back (num ("pan" + std::to_string (k), "PAN " + std::to_string (k), -1.0, 1.0, 0.0));
        for (int k = 1; k <= 4; ++k)
            d.params.push_back (toggle ("mute" + std::to_string (k), "MUTE " + std::to_string (k), false));
        for (int k = 1; k <= 4; ++k)
            d.params.push_back (toggle ("solo" + std::to_string (k), "SOLO " + std::to_string (k), false));
        d.params.push_back (num ("master", "MASTER", -60.0, 6.0, 0.0, "dB"));
        d.cost.units = 0.1f;
        d.create = make<Mixer>();
        r.add (d);
    }
    {
        auto d = utility ("util.switch", "SWITCH", "Selects one of four inputs; between two it crossfades (click-free, scene-friendly)");
        d.keywords = { "switch", "select", "mux", "selector", "crossfade" };
        d.domains = voiceOrGlobal;
        d.inputs = { in ("in1", "1", PortKind::audioMono), in ("in2", "2", PortKind::audioMono), in ("in3", "3", PortKind::audioMono),
                     in ("in4", "4", PortKind::audioMono) };
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        d.params = { num ("select", "SELECT", 0.0, 3.0, 0.0, "", true), toggle ("hard", "STEPPED", false) };
        d.create = make<Switch>();
        r.add (d);
    }
    {
        auto d = utility ("util.split", "STEREO SPLIT", "A stereo bus as LEFT, RIGHT, MID and SIDE mono signals");
        d.keywords = { "split", "stereo", "mid side", "left", "right", "mono" };
        d.inputs = { in ("in", "IN", PortKind::audioStereo) };
        auto mono = [] (const char* id, const char* name) {
            auto p = out (id, name, PortKind::audioMono);
            p.stereoWhenGlobal = false;
            return p;
        };
        d.outputs = { mono ("left", "LEFT"), mono ("right", "RIGHT"), mono ("mid", "MID"), mono ("side", "SIDE") };
        d.create = make<Split>();
        r.add (d);
    }
    {
        auto d = utility ("util.merge", "STEREO MERGE", "Two mono signals as one stereo bus (LEFT/RIGHT or MID/SIDE)");
        d.keywords = { "merge", "stereo", "join", "mid side" };
        auto mono = [] (const char* id, const char* name) {
            auto p = in (id, name, PortKind::audioMono);
            p.stereoWhenGlobal = false;
            return p;
        };
        d.inputs = { mono ("a", "L / MID"), mono ("b", "R / SIDE") };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { toggle ("ms", "MID/SIDE", false) };
        d.create = make<Merge>();
        r.add (d);
    }
    {
        auto d = utility ("util.gain", "GAIN / VCA", "Level in dB, polarity, and a CV input that multiplies (a VCA for any control)");
        d.keywords = { "gain", "vca", "volume", "level", "invert", "polarity", "trim" };
        d.domains = voiceOrGlobal;
        d.inputs = { in ("in", "IN", PortKind::audioMono), in ("cv", "CV", PortKind::control) };
        d.inputs[1].polarity = Polarity::unipolar;
        d.outputs = { out ("out", "OUT", PortKind::audioMono) };
        d.params = { num ("gain", "GAIN", -60.0, 24.0, 0.0, "dB", true), toggle ("invert", "INVERT", false) };
        d.create = make<Gain>();
        r.add (d);
    }
    {
        auto d = utility ("util.feedback", "FEEDBACK DELAY", "The only way to close a loop: a delay of at least one block, gain below 1, DC-safe");
        d.keywords = { "feedback", "loop", "delay", "recursion" };
        d.role = NodeRole::feedback;
        d.inputs = { in ("in", "IN", PortKind::audioStereo) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { logNum ("time", "TIME", 1.0, 2000.0, 120.0, "ms", true), num ("feedback", "AMOUNT", 0.0, 0.95, 0.5, "%", true),
                     num ("damping", "DAMPING", 0.0, 1.0, 0.3, "%") };
        d.cost.units = 0.2f;
        d.create = make<FeedbackDelay>();
        r.add (d);
    }
    {
        auto d = utility ("util.output", "OUTPUT", "The patch's one true output (a safety limiter follows it)");
        d.keywords = { "output", "out", "master", "main" };
        d.role = NodeRole::output;
        d.inputs = { in ("in", "IN", PortKind::audioStereo) };
        d.outputs = { out ("main", "MAIN", PortKind::audioStereo) };
        d.params = { num ("level", "LEVEL", -60.0, 6.0, 0.0, "dB", true), toggle ("mute", "MUTE", false) };
        d.create = make<Output>();
        r.add (d);
    }
    {
        auto d = utility ("util.scope", "SCOPE / METER", "A pass-through with a live meter and a small scope of what passes");
        d.keywords = { "scope", "meter", "level", "analyzer", "oscilloscope" };
        d.inputs = { in ("in", "IN", PortKind::audioStereo) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.create = make<Scope>();
        r.add (d);
    }
}

} // namespace andor::modular
