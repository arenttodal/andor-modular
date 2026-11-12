#include "nodes/NodeKit.h"

#include "nodes/AnalogFx.h"

#include "fx/Effects.h"
#include "fx/FxParameterTable.h"
#include "fx/FxProcessor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>

namespace andor::modular
{
using namespace kit;
namespace afx = andor::analog::fx;

namespace
{
    using Fill = void (*) (afx::Settings&, const float*);

    /** A setting the module had no parameter for before the library's table was shared
        (state schema 2): written through the table, as ANDOR ANALOG writes it. */
    struct Extra
    {
        int param = 0;                          ///< index in the module's parameters
        const afx::ParameterSpec* spec = nullptr;
        float scale = 1.0f;
        float fallback = 0.0f;                  ///< the parameter's default (its own units)
    };
    using Extras = std::shared_ptr<const std::vector<Extra>>;

    /** Starts from the library's built-in settings and writes what the musician moved. A setting
        left at its default is not written: the processor then plays the built-in value itself,
        bit for bit what a schema 1 patch played (the table's 12 % is 0.01f * 12, one float
        apart from the built-in 0.12f). */
    void writeExtras (afx::Settings& s, const std::vector<Extra>& extras, const float* p) noexcept
    {
        for (const auto& e : extras)
            if (p[e.param] != e.fallback)
                e.spec->write (s, 0, p[e.param] * e.scale);
    }

    /** ANDOR ANALOG's FX library (dsp/fx) as global stereo modules: each owns one processor and
        one settings block (slot 0), filled from its parameters every chunk. The parameters a
        module always had are written as they always were (`fill`); the rest of the effect's
        settings through the library's own table. */
    class AnalogFx final : public Processor, public AnalogFxAccess
    {
    public:
        AnalogFx (afx::Type t, Fill f, Extras e) : type (t), fill (f), extras (std::move (e)) {}

        const afx::Processor* analogFx() const noexcept override { return fx.get(); }

        void prepare (const PrepareContext& pc) override
        {
            fx = afx::makeProcessor (type);
            if (fx != nullptr)
                fx->prepare (pc.sampleRate, maxChunk);
            // (what its tile draws live: found once, here, off the audio thread)
            comp = dynamic_cast<afx::Compressor*> (fx.get());
            phaser = dynamic_cast<afx::Phaser*> (fx.get());
            flanger = dynamic_cast<afx::Flanger*> (fx.get());
            spread = dynamic_cast<afx::StereoSpread*> (fx.get());
            retro = dynamic_cast<afx::Retro*> (fx.get());
        }
        /** The processor's own meters (atomics it already keeps for ANDOR Analog's graphs):
            COMPRESSOR gain reduction and input level (dB); PHASER its notches (Hz); FLANGER its
            delay now (ms); SPREAD correlation, side and mid level; RETRO bits, rate (kHz), wow
            (cents), kept fraction. */
        void telemetry (Telemetry& t) const noexcept override
        {
            if (comp != nullptr)
            {
                t.values[0] = comp->reductionDb.load (std::memory_order_relaxed);
                t.values[1] = comp->inputDb.load (std::memory_order_relaxed);
                t.count = 2;
            }
            else if (phaser != nullptr)
            {
                for (std::size_t i = 0; i < 6; ++i)
                    t.values[i] = phaser->notchHz[i].load (std::memory_order_relaxed);
                t.count = 6;
            }
            else if (flanger != nullptr)
            {
                t.values[0] = flanger->delayNowMs.load (std::memory_order_relaxed);
                t.count = 1;
            }
            else if (spread != nullptr)
            {
                t.values[0] = spread->correlation.load (std::memory_order_relaxed);
                t.values[1] = spread->sideLevel.load (std::memory_order_relaxed);
                t.values[2] = spread->midLevel.load (std::memory_order_relaxed);
                t.count = 3;
            }
            else if (retro != nullptr)
            {
                t.values[0] = retro->effectiveBits.load (std::memory_order_relaxed);
                t.values[1] = retro->effectiveRateKHz.load (std::memory_order_relaxed);
                t.values[2] = retro->wowCents.load (std::memory_order_relaxed);
                t.values[3] = retro->keptFraction.load (std::memory_order_relaxed);
                t.count = 4;
            }
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
            settings = builtIn;
            settings.bpm = ctx.bpm;
            fill (settings, io.params);
            writeExtras (settings, *extras, io.params);
            fx->set (settings, 0);
            fx->process (io.out[0][0], io.out[0][1], ctx.numSamples);
        }
        int latency() const noexcept override { return fx != nullptr ? fx->maxLatency() : 0; }

    private:
        afx::Type type;
        Fill fill;
        Extras extras;
        std::unique_ptr<afx::Processor> fx;
        const afx::Settings builtIn {};
        afx::Settings settings;
        const afx::Compressor* comp = nullptr;
        const afx::Phaser* phaser = nullptr;
        const afx::Flanger* flanger = nullptr;
        const afx::StereoSpread* spread = nullptr;
        const afx::Retro* retro = nullptr;
    };

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

    /** CONVOLUTION's impulse is built at compile (rebuilding its partitions allocates): the
        factory impulse, its DECAY, SIZE, REVERSE and NORMALIZE are structural. */
    class Convolution final : public Processor, public AnalogFxAccess
    {
    public:
        explicit Convolution (Extras e) : extras (std::move (e)) {}

        const afx::Processor* analogFx() const noexcept override { return fx.get(); }
        std::shared_ptr<const afx::Impulse> analogImpulse() const override { return impulse; }

        void prepare (const PrepareContext& pc) override
        {
            fx = afx::makeProcessor (afx::Type::convolution);
            fx->prepare (pc.sampleRate, maxChunk);
            rate = pc.sampleRate;
            if (pc.params != nullptr)
            {
                fill (pc.params);
                prepareImpulse (static_cast<int> (pc.params[0]), pc.params[6]);
            }
        }
        void reset() noexcept override { fx->reset(); }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            fill (io.params);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (io.in[0][static_cast<std::size_t> (ch)], io.in[0][static_cast<std::size_t> (ch)] + ctx.numSamples,
                           io.out[0][static_cast<std::size_t> (ch)]);
            fx->set (settings, 0);
            fx->process (io.out[0][0], io.out[0][1], ctx.numSamples);
        }
        int latency() const noexcept override { return fx->maxLatency(); }

    private:
        void fill (const float* p) noexcept
        {
            const int index = settings.conv[0].impulse;
            const float decay = settings.conv[0].decaySeconds;
            settings = builtIn;
            auto& c = settings.conv[0];
            c.impulse = index;         // (structural: set with the impulse, prepareImpulse)
            c.decaySeconds = decay;
            c.mix = p[1];
            c.preDelayMs = p[2];
            c.width = p[3];
            c.lowCutHz = p[4];
            c.highCutHz = p[5];
            writeExtras (settings, *extras, p);
        }
        void prepareImpulse (int index, double decay)
        {
            auto& c = settings.conv[0];
            c.impulse = index;
            c.decaySeconds = static_cast<float> (decay);
            impulse = afx::Impulse::make (afx::Impulse::factory (index, rate), rate, c, rate, 1, afx::Impulse::factoryName (index));
            if (auto* conv = dynamic_cast<afx::Convolution*> (fx.get()))
                conv->setImpulse (impulse);
        }

        Extras extras;
        std::unique_ptr<afx::Processor> fx;
        std::shared_ptr<const afx::Impulse> impulse;
        const afx::Settings builtIn {};
        afx::Settings settings;
        double rate = 48000.0;
    };

    //==============================================================================
    // The library's table on a module: the parameters it always had keep their ids, units and
    // order (`legacy`: the table's name of each, and how the table counts it); every other
    // setting of the effect follows as a parameter of the table's own name.
    struct LegacyLink
    {
        const char* param;
        const char* setting;
        double scale;
    };
    struct Bound
    {
        afx::Type type = afx::Type::eq;
        std::vector<AnalogFxLink> links;
    };
    std::map<std::string, Bound>& bindings()
    {
        static std::map<std::string, Bound> all;
        return all;
    }

    /** A parameter of this module for one of the table's settings (a detail: it is edited in the
        module's rack; EVOLVE leaves it alone). Percentages are 0..1 here, as everywhere in
        ANDOR Modular. */
    ParamDesc fromSpec (const afx::ParameterSpec& spec, double& scale)
    {
        using Kind = afx::ParameterSpec::Kind;
        auto caps = [] (std::string t) {
            for (auto& ch : t)
                ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
            return t;
        };
        scale = 1.0;
        ParamDesc p;
        if (spec.kind == Kind::choice)
        {
            std::vector<std::string> items;
            for (const auto& c : spec.choices)
                items.push_back (caps (c));
            p = choice (spec.name, caps (spec.title), std::move (items), static_cast<int> (spec.fallback));
        }
        else if (spec.kind == Kind::toggle)
            p = toggle (spec.name, caps (spec.title), spec.fallback >= 0.5f);
        else
        {
            const bool percent = spec.unit == "%";
            scale = percent ? 100.0 : 1.0;
            std::string unit = spec.unit == "cents" ? "ct" : (spec.unit == "x" ? "" : (spec.unit == "#" ? "" : spec.unit));
            // (a skewed range there is a logarithmic one here: positive, with a centre below the middle)
            const bool log = ! percent && spec.low > 0.0f && spec.centre > spec.low && spec.centre < 0.5f * (spec.low + spec.high);
            p = num (spec.name, caps (spec.title), spec.low / scale, spec.high / scale, spec.fallback / scale, std::move (unit), false,
                     log ? ParamCurve::logarithmic : ParamCurve::linear);
        }
        p.detail = true;
        return p;
    }

    /** Completes `d` (whose parameters are the module's own, `legacy` naming their settings)
        with the rest of the effect's settings and returns what the processor writes of them.
        `structural`: settings that rebuild the module (CONVOLUTION's impulse). */
    Extras bind (NodeDesc& d, afx::Type type, std::vector<LegacyLink> legacy, std::vector<std::string> structural = {})
    {
        Bound bound;
        bound.type = type;
        for (const auto& l : legacy)
            bound.links.push_back ({ l.setting, l.param, l.scale });
        auto extras = std::make_shared<std::vector<Extra>>();
        for (const auto& spec : afx::parameterSpecs (type))
        {
            if (spec.name == "on")
                continue;   // (the module's light is its bypass)
            if (std::any_of (legacy.begin(), legacy.end(), [&] (const LegacyLink& l) { return spec.name == l.setting; }))
                continue;
            double scale = 1.0;
            auto p = fromSpec (spec, scale);
            if (std::find (structural.begin(), structural.end(), spec.name) != structural.end())
            {
                p.structural = true;
                p.modulatable = false;
            }
            extras->push_back ({ static_cast<int> (d.params.size()), &spec, static_cast<float> (scale), static_cast<float> (p.def) });
            bound.links.push_back ({ spec.name, p.id, scale });
            d.params.push_back (std::move (p));
        }
        bindings()[d.type] = std::move (bound);
        return extras;
    }

    void add (Registry& r, NodeDesc d, afx::Type type, Fill fill, std::vector<LegacyLink> legacy)
    {
        auto extras = bind (d, type, std::move (legacy));
        d.create = [type, fill, extras] { return std::make_unique<AnalogFx> (type, fill, extras); };
        r.add (d);
    }
}

bool analogFxType (const std::string& nodeType, afx::Type& type)
{
    const auto it = bindings().find (nodeType);
    if (it == bindings().end())
        return false;
    type = it->second.type;
    return true;
}

const std::vector<AnalogFxLink>& analogFxLinks (const std::string& nodeType)
{
    static const std::vector<AnalogFxLink> none;
    const auto it = bindings().find (nodeType);
    return it != bindings().end() ? it->second.links : none;
}

void registerFxNodes (Registry& r)
{
    {
        auto d = fxBase ("fx.phaser", "PHASER", "ANDOR ANALOG's phaser: CLASSIC or DEEP, 2-12 stages, synced or free", { "phaser", "phase", "sweep" });
        d.params = { choice ("mode", "MODE", { "CLASSIC", "DEEP" }, 0), logNum ("rate", "RATE", 0.03, 10.0, 0.5, "Hz", true),
                     num ("depth", "DEPTH", 0.0, 1.0, 0.6, "%", true), logNum ("centre", "CENTRE", 80.0, 8000.0, 800.0, "Hz"),
                     num ("feedback", "FEEDBACK", -0.85, 0.95, 0.35, "%"), choice ("stages", "STAGES", { "2", "4", "6", "8", "12" }, 1),
                     num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        add (r, d, afx::Type::phaser,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.phaser[0];
            x.mode = static_cast<int> (p[0]);
            x.rateHz = p[1];
            x.depth = p[2];
            x.centreHz = p[3];
            x.feedback = p[4];
            x.stages = static_cast<int> (p[5]);
            x.mix = p[6];
        },
             { { "mode", "mode", 1.0 }, { "rate", "rate", 1.0 }, { "depth", "depth", 100.0 }, { "centre", "center", 1.0 }, { "feedback", "feedback", 100.0 }, { "stages", "stages", 1.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.flanger", "FLANGER", "ANDOR ANALOG's flanger: CLASSIC or THROUGH-ZERO", { "flanger", "jet", "comb", "sweep" });
        d.params = { choice ("mode", "MODE", { "CLASSIC", "THROUGH ZERO" }, 0), logNum ("rate", "RATE", 0.03, 10.0, 0.25, "Hz", true),
                     num ("depth", "DEPTH", 0.0, 1.0, 0.5, "%", true), num ("delay", "DELAY", 0.2, 12.0, 2.0, "ms"),
                     num ("feedback", "FEEDBACK", -0.95, 0.95, 0.4, "%"), num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        add (r, d, afx::Type::flanger,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.flanger[0];
            x.mode = static_cast<int> (p[0]);
            x.rateHz = p[1];
            x.depth = p[2];
            x.delayMs = p[3];
            x.feedback = p[4];
            x.mix = p[5];
        },
             { { "mode", "mode", 1.0 }, { "rate", "rate", 1.0 }, { "depth", "depth", 100.0 }, { "delay", "delay", 1.0 }, { "feedback", "feedback", 100.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.shifter", "FREQUENCY SHIFTER", "Shifts every partial by the same Hz (inharmonic, bell-like) or BLURs", { "frequency shifter", "shift", "ring", "blur" });
        d.params = { choice ("mode", "MODE", { "SHIFT", "BLUR" }, 0), num ("shift", "SHIFT", 0.0, 2000.0, 100.0, "Hz", true), toggle ("down", "DOWN", false),
                     num ("width", "WIDTH", 0.0, 2.0, 1.0, "%"), num ("feedback", "FEEDBACK", 0.0, 0.8, 0.0, "%"), num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        add (r, d, afx::Type::shifter,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.shifter[0];
            x.mode = static_cast<int> (p[0]);
            x.shiftHz = p[1];
            x.down = p[2] >= 0.5f;
            x.width = p[3];
            x.feedback = p[4];
            x.mix = p[5];
        },
             { { "mode", "mode", 1.0 }, { "shift", "hz", 1.0 }, { "down", "direction", 1.0 }, { "width", "width", 100.0 }, { "feedback", "feedback", 100.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.compressor", "COMPRESSOR", "CLEAN (transparent), FET character or MULTIBAND (three bands, up and down) compression", { "compressor", "dynamics", "glue", "punch", "fet" });
        d.params = { choice ("mode", "MODE", { "CLEAN", "FET", "MULTIBAND" }, 0, true), num ("threshold", "THRESHOLD", -60.0, 0.0, -18.0, "dB", true),
                     num ("ratio", "RATIO", 1.0, 20.0, 3.0, ":1", true), logNum ("attack", "ATTACK", 0.1, 100.0, 10.0, "ms"),
                     logNum ("release", "RELEASE", 10.0, 2000.0, 180.0, "ms"), num ("makeup", "MAKEUP", -12.0, 24.0, 0.0, "dB"), num ("mix", "MIX", 0.0, 1.0, 1.0, "%") };
        add (r, d, afx::Type::compressor,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.comp[0];
            x.mode = static_cast<int> (p[0]);
            x.thresholdDb = p[1];
            x.ratio = p[2];
            x.attackMs = p[3];
            x.releaseMs = p[4];
            x.makeupDb = p[5];
            x.mix = p[6];
        },
             { { "mode", "mode", 1.0 }, { "threshold", "threshold", 1.0 }, { "ratio", "ratio", 1.0 }, { "attack", "attack", 1.0 }, { "release", "release", 1.0 }, { "makeup", "makeup", 1.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.filter", "FX FILTER", "Stereo filter: low/high/band, notch, comb and formant (vowel) types", { "filter", "comb", "formant", "vowel", "notch" });
        d.params = { choice ("type", "TYPE", { "LOW 12", "LOW 24", "HIGH 12", "HIGH 24", "BAND", "NOTCH", "ALLPASS", "COMB +", "COMB -", "VOWEL A", "VOWEL E", "VOWEL O" }, 0, true),
                     logNum ("hz", "FREQ", 20.0, 20000.0, 1000.0, "Hz", true), num ("resonance", "RESONANCE", 0.0, 0.95, 0.2, "%", true),
                     num ("drive", "DRIVE", 0.0, 24.0, 0.0, "dB"), num ("mix", "MIX", 0.0, 1.0, 1.0, "%") };
        add (r, d, afx::Type::filter,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.filter[0];
            x.type = static_cast<int> (p[0]);
            x.hz = p[1];
            x.resonance = p[2];
            x.driveDb = p[3];
            x.mix = p[4];
        },
             { { "type", "type", 1.0 }, { "hz", "freq", 1.0 }, { "resonance", "reso", 100.0 }, { "drive", "drive", 1.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.retro", "RETRO", "One aging machine: TAPE, VINYL or DIGITAL (bits, codec)", { "retro", "lofi", "vinyl", "tape", "bitcrush", "codec" });
        d.params = { choice ("mode", "MODE", { "TAPE", "VINYL", "DIGITAL" }, 0, true), num ("character", "CHARACTER", 0.0, 1.0, 0.4, "%", true),
                     num ("wear", "WEAR", 0.0, 1.0, 0.25, "%", true), num ("saturation", "SATURATION", 0.0, 1.0, 0.2, "%"),
                     num ("noise", "NOISE", -96.0, -20.0, -96.0, "dB"), num ("bits", "BITS", 4.0, 16.0, 12.0, "bit"), num ("mix", "MIX", 0.0, 1.0, 0.65, "%") };
                d.cost.units = 1.2f;
add (r, d, afx::Type::retro,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.retro[0];
            x.mode = static_cast<int> (p[0]);
            x.character = p[1];
            x.wear = p[2];
            x.saturation = p[3];
            x.noiseDb = p[4];
            x.bits = p[5];
            x.mix = p[6];
        },
             { { "mode", "mode", 1.0 }, { "character", "character", 100.0 }, { "wear", "wear", 100.0 }, { "saturation", "saturation", 100.0 }, { "noise", "noise.level", 1.0 }, { "bits", "digital.bits", 1.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.spread", "STEREO SPREAD", "HYPER (detuned heads) and DIMENSION width, bass kept mono", { "spread", "width", "stereo", "dimension", "hyper" });
        d.params = { choice ("mode", "MODE", { "HYPER", "DIMENSION", "BOTH" }, 2, true), num ("width", "WIDTH", 0.0, 2.0, 1.0, "%", true),
                     num ("detune", "DETUNE", 0.0, 60.0, 8.0, "ct"), num ("mix", "MIX", 0.0, 1.0, 0.5, "%", true) };
        add (r, d, afx::Type::spread,
             [] (afx::Settings& s, const float* p) {
            auto& x = s.spread[0];
            x.mode = static_cast<int> (p[0]);
            x.width = p[1];
            x.detuneCents = p[2];
            x.mix = p[3];
        },
             { { "mode", "mode", 1.0 }, { "width", "width", 100.0 }, { "detune", "detune", 1.0 }, { "mix", "mix", 100.0 } });
    }
    {
        auto d = fxBase ("fx.convolution", "CONVOLUTION", "Real spaces and bodies from ANDOR's factory impulses (partitioned)", { "convolution", "impulse", "ir", "convolution reverb", "body" });
        d.category = "Spatial";
        std::vector<std::string> impulses;
        for (int i = 0; i < afx::Impulse::factoryCount; ++i)
            impulses.emplace_back (afx::Impulse::factoryName (i));
        d.params = { choice ("impulse", "IMPULSE", impulses, 1, true), num ("mix", "MIX", 0.0, 1.0, 0.3, "%", true), num ("predelay", "PRE-DELAY", 0.0, 250.0, 0.0, "ms"),
                     num ("width", "WIDTH", 0.0, 2.0, 1.0, "%"), logNum ("lowcut", "LOW CUT", 20.0, 2000.0, 20.0, "Hz"),
                     logNum ("highcut", "HIGH CUT", 1000.0, 20000.0, 20000.0, "Hz"), logNum ("decay", "DECAY", 0.1, 20.0, 4.0, "s") };
        d.params[0].structural = true;
        d.params[6].structural = true;
        d.params[6].modulatable = false;
        d.cost.units = 2.0f;
        d.cost.heavy = true;
        const auto extras = bind (d, afx::Type::convolution,
                                  { { "impulse", "impulse", 1.0 }, { "mix", "mix", 100.0 }, { "predelay", "predelay", 1.0 }, { "width", "width", 100.0 },
                                    { "lowcut", "lowCut", 1.0 }, { "highcut", "highCut", 1.0 }, { "decay", "decay", 1.0 } },
                                  { "size", "normalize", "reverse" });
        d.create = [extras] { return std::make_unique<Convolution> (extras); };
        r.add (d);
    }
}

} // namespace andor::modular
