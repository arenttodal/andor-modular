#include "nodes/NodeKit.h"

#include "fx/Effects.h"
#include "fx/FxProcessor.h"

#include <array>

namespace andor::modular
{
using namespace kit;
namespace afx = andor::analog::fx;

namespace
{
    /** ANDOR ANALOG's FX library (dsp/fx) as global stereo modules: each owns one processor and
        one settings block (slot 0), filled from its parameters every chunk. */
    template <afx::Type T>
    class AnalogFx final : public Processor
    {
    public:
        using Fill = void (*) (afx::Settings&, const float*);
        explicit AnalogFx (Fill f) : fill (f) {}

        void prepare (const PrepareContext& pc) override
        {
            fx = afx::makeProcessor (T);
            if (fx != nullptr)
                fx->prepare (pc.sampleRate, maxChunk);
        }
        void reset() noexcept override
        {
            if (fx != nullptr)
                fx->reset();
        }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            for (int c = 0; c < 2; ++c)
                std::copy (io.in[0][static_cast<std::size_t> (c)], io.in[0][static_cast<std::size_t> (c)] + ctx.numSamples,
                           io.out[0][static_cast<std::size_t> (c)]);
            if (fx == nullptr)
                return;
            settings.bpm = ctx.bpm;
            fill (settings, io.params);
            fx->set (settings, 0);
            fx->process (io.out[0][0], io.out[0][1], ctx.numSamples);
        }
        int latency() const noexcept override { return fx != nullptr ? fx->maxLatency() : 0; }

    private:
        Fill fill;
        std::unique_ptr<afx::Processor> fx;
        afx::Settings settings;
    };

    template <afx::Type T>
    std::function<std::unique_ptr<Processor>()> makeFx (typename AnalogFx<T>::Fill f)
    {
        return [f] { return std::make_unique<AnalogFx<T>> (f); };
    }

    NodeDesc fxBase (const char* type, const char* name, const char* description, std::vector<std::string> keywords)
    {
        NodeDesc d;
        d.type = type;
        d.name = name;
        d.description = description;
        d.category = "Processors";
        d.keywords = std::move (keywords);
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.inputs = { in ("in", "IN", PortKind::audioStereo, true) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.cost.units = 0.8f;
        return d;
    }

    /** CONVOLUTION's factory impulse is chosen at compile (rebuilding its partitions allocates). */
    class Convolution final : public Processor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            fx = afx::makeProcessor (afx::Type::convolution);
            fx->prepare (pc.sampleRate, maxChunk);
            rate = pc.sampleRate;
            if (pc.params != nullptr)
                prepareImpulse (static_cast<int> (pc.params[0]), pc.params[6]);
        }
        void reset() noexcept override { fx->reset(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            auto& c = settings.conv[0];
            c.mix = p[1];
            c.preDelayMs = p[2];
            c.width = p[3];
            c.lowCutHz = p[4];
            c.highCutHz = p[5];
            for (int ch = 0; ch < 2; ++ch)
                std::copy (io.in[0][static_cast<std::size_t> (ch)], io.in[0][static_cast<std::size_t> (ch)] + ctx.numSamples,
                           io.out[0][static_cast<std::size_t> (ch)]);
            fx->set (settings, 0);
            fx->process (io.out[0][0], io.out[0][1], ctx.numSamples);
        }
        int latency() const noexcept override { return fx->maxLatency(); }
        void prepareImpulse (int index, double decay)
        {
            auto& c = settings.conv[0];
            c.impulse = index;
            c.decaySeconds = static_cast<float> (decay);
            if (auto* conv = dynamic_cast<afx::Convolution*> (fx.get()))
                conv->setImpulse (afx::Impulse::make (afx::Impulse::factory (index, rate), rate, c, rate, 1, afx::Impulse::factoryName (index)));
        }

    private:
        std::unique_ptr<afx::Processor> fx;
        afx::Settings settings;
        double rate = 48000.0;
    };
}

void registerFxNodes (Registry& r)
{
    {
        auto d = fxBase ("fx.phaser", "PHASER", "ANDOR ANALOG's phaser: CLASSIC or DEEP, 2-12 stages, synced or free", { "phaser", "phase", "sweep" });
        d.params = { choice ("mode", "MODE", { "CLASSIC", "DEEP" }, 0), logNum ("rate", "RATE", 0.03, 10.0, 0.5, "Hz", true),
                     num ("depth", "DEPTH", 0.0, 1.0, 0.6, "%", true), logNum ("centre", "CENTRE", 100.0, 5000.0, 800.0, "Hz"),
                     num ("feedback", "FEEDBACK", 0.0, 0.95, 0.35, "%"), choice ("stages", "STAGES", { "2", "4", "6", "8", "12" }, 1),
                     num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        d.create = makeFx<afx::Type::phaser> ([] (afx::Settings& s, const float* p) {
            auto& x = s.phaser[0];
            x.mode = static_cast<int> (p[0]);
            x.rateHz = p[1];
            x.depth = p[2];
            x.centreHz = p[3];
            x.feedback = p[4];
            x.stages = static_cast<int> (p[5]);
            x.mix = p[6];
        });
        r.add (d);
    }
    {
        auto d = fxBase ("fx.flanger", "FLANGER", "ANDOR ANALOG's flanger: CLASSIC or THROUGH-ZERO", { "flanger", "jet", "comb", "sweep" });
        d.params = { choice ("mode", "MODE", { "CLASSIC", "THROUGH ZERO" }, 0), logNum ("rate", "RATE", 0.03, 10.0, 0.25, "Hz", true),
                     num ("depth", "DEPTH", 0.0, 1.0, 0.5, "%", true), num ("delay", "DELAY", 0.5, 10.0, 2.0, "ms"),
                     num ("feedback", "FEEDBACK", -0.95, 0.95, 0.4, "%"), num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        d.create = makeFx<afx::Type::flanger> ([] (afx::Settings& s, const float* p) {
            auto& x = s.flanger[0];
            x.mode = static_cast<int> (p[0]);
            x.rateHz = p[1];
            x.depth = p[2];
            x.delayMs = p[3];
            x.feedback = p[4];
            x.mix = p[5];
        });
        r.add (d);
    }
    {
        auto d = fxBase ("fx.shifter", "FREQUENCY SHIFTER", "Shifts every partial by the same Hz (inharmonic, bell-like) or BLURs", { "frequency shifter", "shift", "ring", "blur" });
        d.params = { choice ("mode", "MODE", { "SHIFT", "BLUR" }, 0), num ("shift", "SHIFT", 0.0, 2000.0, 100.0, "Hz", true), toggle ("down", "DOWN", false),
                     num ("width", "WIDTH", 0.0, 2.0, 1.0, "%"), num ("feedback", "FEEDBACK", 0.0, 0.8, 0.0, "%"), num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        d.create = makeFx<afx::Type::shifter> ([] (afx::Settings& s, const float* p) {
            auto& x = s.shifter[0];
            x.mode = static_cast<int> (p[0]);
            x.shiftHz = p[1];
            x.down = p[2] >= 0.5f;
            x.width = p[3];
            x.feedback = p[4];
            x.mix = p[5];
        });
        r.add (d);
    }
    {
        auto d = fxBase ("fx.compressor", "COMPRESSOR", "CLEAN (transparent) or FET character compression", { "compressor", "dynamics", "glue", "punch", "fet" });
        d.params = { choice ("mode", "MODE", { "CLEAN", "FET" }, 0, true), num ("threshold", "THRESHOLD", -48.0, 0.0, -18.0, "dB", true),
                     num ("ratio", "RATIO", 1.0, 20.0, 3.0, ":1", true), logNum ("attack", "ATTACK", 0.1, 100.0, 10.0, "ms"),
                     logNum ("release", "RELEASE", 10.0, 2000.0, 180.0, "ms"), num ("makeup", "MAKEUP", 0.0, 24.0, 0.0, "dB"), num ("mix", "MIX", 0.0, 1.0, 1.0, "%") };
        d.create = makeFx<afx::Type::compressor> ([] (afx::Settings& s, const float* p) {
            auto& x = s.comp[0];
            x.mode = static_cast<int> (p[0]);
            x.thresholdDb = p[1];
            x.ratio = p[2];
            x.attackMs = p[3];
            x.releaseMs = p[4];
            x.makeupDb = p[5];
            x.fetInputDb = std::max (0.0f, -p[1]);
            x.mix = p[6];
        });
        r.add (d);
    }
    {
        auto d = fxBase ("fx.filter", "FX FILTER", "Stereo filter: low/high/band, notch, comb and formant (vowel) types", { "filter", "comb", "formant", "vowel", "notch" });
        d.params = { choice ("type", "TYPE", { "LOW 12", "LOW 24", "HIGH 12", "HIGH 24", "BAND", "NOTCH", "ALLPASS", "COMB +", "COMB -", "VOWEL A", "VOWEL E", "VOWEL O" }, 0, true),
                     logNum ("hz", "FREQ", 20.0, 20000.0, 1000.0, "Hz", true), num ("resonance", "RESONANCE", 0.0, 0.95, 0.2, "%", true),
                     num ("drive", "DRIVE", 0.0, 24.0, 0.0, "dB"), num ("mix", "MIX", 0.0, 1.0, 1.0, "%") };
        d.create = makeFx<afx::Type::filter> ([] (afx::Settings& s, const float* p) {
            auto& x = s.filter[0];
            x.type = static_cast<int> (p[0]);
            x.hz = p[1];
            x.resonance = p[2];
            x.driveDb = p[3];
            x.mix = p[4];
        });
        r.add (d);
    }
    {
        auto d = fxBase ("fx.retro", "RETRO", "One aging machine: TAPE, VINYL or DIGITAL (bits, codec)", { "retro", "lofi", "vinyl", "tape", "bitcrush", "codec" });
        d.params = { choice ("mode", "MODE", { "TAPE", "VINYL", "DIGITAL" }, 0, true), num ("character", "CHARACTER", 0.0, 1.0, 0.4, "%", true),
                     num ("wear", "WEAR", 0.0, 1.0, 0.25, "%", true), num ("saturation", "SATURATION", 0.0, 1.0, 0.2, "%"),
                     num ("noise", "NOISE", -96.0, -20.0, -96.0, "dB"), num ("bits", "BITS", 4.0, 16.0, 12.0, "bit"), num ("mix", "MIX", 0.0, 1.0, 0.65, "%") };
        d.create = makeFx<afx::Type::retro> ([] (afx::Settings& s, const float* p) {
            auto& x = s.retro[0];
            x.mode = static_cast<int> (p[0]);
            x.character = p[1];
            x.wear = p[2];
            x.saturation = p[3];
            x.noiseDb = p[4];
            x.bits = p[5];
            x.mix = p[6];
        });
        d.cost.units = 1.2f;
        r.add (d);
    }
    {
        auto d = fxBase ("fx.spread", "STEREO SPREAD", "HYPER (detuned heads) and DIMENSION width, bass kept mono", { "spread", "width", "stereo", "dimension", "hyper" });
        d.params = { choice ("mode", "MODE", { "HYPER", "DIMENSION", "BOTH" }, 2, true), num ("width", "WIDTH", 0.0, 2.0, 1.0, "%", true),
                     num ("detune", "DETUNE", 0.0, 60.0, 8.0, "ct"), num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        d.create = makeFx<afx::Type::spread> ([] (afx::Settings& s, const float* p) {
            auto& x = s.spread[0];
            x.mode = static_cast<int> (p[0]);
            x.width = p[1];
            x.detuneCents = p[2];
            x.mix = p[3];
        });
        r.add (d);
    }
    {
        auto d = fxBase ("fx.convolution", "CONVOLUTION", "Real spaces and bodies from ANDOR's factory impulses (partitioned)", { "convolution", "impulse", "ir", "convolution reverb", "body" });
        d.category = "Spatial";
        std::vector<std::string> impulses;
        for (int i = 0; i < afx::Impulse::factoryCount; ++i)
            impulses.emplace_back (afx::Impulse::factoryName (i));
        d.params = { choice ("impulse", "IMPULSE", impulses, 1, true), num ("mix", "MIX", 0.0, 1.0, 0.3, "%", true), num ("predelay", "PRE-DELAY", 0.0, 200.0, 0.0, "ms"),
                     num ("width", "WIDTH", 0.0, 1.0, 1.0, "%"), logNum ("lowcut", "LOW CUT", 20.0, 1000.0, 20.0, "Hz"),
                     logNum ("highcut", "HIGH CUT", 1000.0, 20000.0, 20000.0, "Hz"), logNum ("decay", "DECAY", 0.2, 8.0, 4.0, "s") };
        d.params[0].structural = true;
        d.params[6].structural = true;
        d.params[6].modulatable = false;
        d.cost.units = 2.0f;
        d.cost.heavy = true;
        d.create = make<Convolution>();
        r.add (d);
    }
}

} // namespace andor::modular
