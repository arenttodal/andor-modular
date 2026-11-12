#include "Racks.h"

#include "FxIdentity.h"
#include "MacroPopups.h"

namespace andor::modular::ui
{

namespace
{
    namespace an = andor::analog;
    using osp::plugin::InstrumentHost;
    using osp::plugin::rack::Kind;
    using Builder = std::function<std::unique_ptr<NativeRack> (Session&, const std::string& nodeId)>;

    RackParam same (const char* rackId, const char* param) { return { rackId, param, 1.0, 0.0, {}, {} }; }
    /** The rack counts in percent, the module 0..1. */
    RackParam percent (const char* rackId, const char* param) { return { rackId, param, 100.0, 0.0, {}, {} }; }

    /** What the module's output is doing now (its peak in the running graph): 0 when silent. */
    std::function<float()> peakOf (Session& s, const std::string& id)
    {
        return [&s, id] {
            if (const auto* g = s.graph())
                if (const auto it = g->nodeIndexById.find (id); it != g->nodeIndexById.end())
                    return g->nodes[static_cast<std::size_t> (it->second)].peak.load (std::memory_order_relaxed);
            return 0.0f;
        };
    }

    /** One of the three racks ANDOR OSP and Analog share (MacroPopups.cpp: EchoRack, SpaceRack,
        MovementRack), in Analog's faceplate for that device, on the ANDOR parameter schema. */
    std::unique_ptr<NativeRack> shared (Session& s, const std::string& id, Kind kind, an::fx::Type identity, std::vector<RackParam> binding, bool tapeDeck)
    {
        auto rack = std::make_unique<NativeRack>();
        rack->host = std::make_unique<RackHost> (s, id, InstrumentHost::createParameterLayout(), std::move (binding));
        auto plate = an::faceplateFromIdentity (an::visualIdentityFor (identity));
        plate.tapeDeck = tapeDeck;
        rack->host->setFaceplate (static_cast<int> (kind), std::move (plate));
        rack->host->outputPeakNow = peakOf (s, id);
        rack->make = [host = rack->host.get(), kind] { return osp::plugin::createRackModule (kind, *host); };
        rack->provenance = "shared";
        return rack;
    }

    //==============================================================================================
    // ECHO: ANDOR's own ECHO rack. Every control is this module's parameter; the macro's amount
    // the rack lights its LED and draws its repeats from is the module's MIX.
    std::unique_ptr<NativeRack> echo (Session& s, const std::string& id)
    {
        return shared (s, id, Kind::echo, an::fx::Type::echo,
                       { same ("echo.type", "type"), same ("echo.sync", "sync"), same ("echo.division", "division"), same ("echo.time", "time"),
                         percent ("echo.feedback", "feedback"), percent ("echo.tone", "tone"), percent ("echo.age", "age"), same ("echo.stereo", "stereo"),
                         same ("echo.ascend.interval", "interval"), same ("echo.ascend.motion", "motion"), percent ("echo", "mix") },
                       true);
    }

    //==============================================================================================
    // SPACE: ANDOR's own SPACE rack. The rack keeps a room (ROOM, HALL, PLATE, SPRING) and an
    // engine (ROOMS, ABYSS, VINTAGE, BLOOM) apart; the module has one TYPE for both.
    std::unique_ptr<NativeRack> space (Session& s, const std::string& id)
    {
        const auto type = [&s, id] { return juce::roundToInt (s.document().paramValue (s.registry(), id, "type")); };
        auto room = std::make_shared<int> (std::min (3, type()) == type() ? type() : 1);   // the room the rack shows while another engine plays
        RackParam engine { "space.engine", "type", 1.0, 0.0, {}, {} };
        engine.toRack = [] (double t) { return t < 3.5 ? 0.0 : t - 3.0; };
        engine.toNode = [room] (double e) { return e < 0.5 ? static_cast<double> (*room) : e + 3.0; };
        RackParam rooms { "space.type", "type", 1.0, 0.0, {}, {} };
        rooms.toRack = [room] (double t) {
            if (t < 3.5)
                *room = juce::roundToInt (t);
            return static_cast<double> (*room);
        };
        rooms.toNode = [room, type] (double r) {
            *room = juce::roundToInt (r);
            return type() < 4 ? r : static_cast<double> (type());   // (another engine plays: the room is kept for when ROOMS returns)
        };
        return shared (s, id, Kind::space, an::fx::Type::space,
                       { engine, rooms, same ("space.decay", "decay"), same ("space.preDelay", "predelay"), percent ("space.size", "size"),
                         percent ("space.damping", "damping"), percent ("space.modulation", "modulation"), percent ("space.width", "width"),
                         same ("space.lowCut", "lowcut"), same ("space.highCut", "highcut"), percent ("space.abyss.gravity", "gravity"),
                         percent ("space.abyss.resonance", "resonance"), same ("space.abyss.freeze", "freeze"), same ("space.abyss.variant", "abyssVariant"),
                         same ("space.vintage.variant", "vintageVariant"), same ("space.vintage.era", "vintageEra"), percent ("space.vintage.diffusion", "vintageDiffusion"),
                         same ("space.bloom.variant", "bloomVariant"), same ("space.bloom.sync", "bloomSync"), same ("space.bloom.division", "bloomDivision"),
                         same ("space.bloom.time", "bloomTime"), percent ("space.bloom.diffusion", "bloomDiffusion"), percent ("space", "mix") },
                       false);
    }

    //==============================================================================================
    // MOVEMENT: ANDOR's own MOVEMENT rack. DRIFT, TAPE, CHORUS and PULSE each show three
    // controls of their own names; the module has one set of three (A, B, C) that the chosen
    // mode reads, so a control keeps its position from mode to mode. SHAPER is its own.
    std::unique_ptr<NativeRack> movement (Session& s, const std::string& id)
    {
        std::vector<RackParam> binding { same ("movement.mode", "mode"), percent ("motion", "amount"), same ("movement.shaper.pattern", "pattern"),
                                         same ("movement.shaper.rate", "shaperRate"), same ("movement.shaper.target", "target"),
                                         percent ("movement.shaper.smooth", "smooth"), same ("movement.shaper.custom", "custom") };
        for (const char* first : { "movement.drift.speed", "movement.tape.wow", "movement.chorus.rate", "movement.pulse.rate" })
            binding.push_back (percent (first, "a"));
        for (const char* second : { "movement.drift.pitch", "movement.tape.flutter", "movement.chorus.width", "movement.pulse.shape" })
            binding.push_back (percent (second, "b"));
        for (const char* third : { "movement.drift.tone", "movement.tape.wear", "movement.chorus.stereo", "movement.pulse.stereo" })
            binding.push_back (percent (third, "c"));
        auto rack = shared (s, id, Kind::movement, an::fx::Type::movement, std::move (binding), true);
        rack->host->shaperPhaseNow = [&s, id] {
            if (const auto* g = s.graph())
                if (const auto it = g->nodeIndexById.find (id); it != g->nodeIndexById.end())
                {
                    const auto& live = g->nodes[static_cast<std::size_t> (it->second)];
                    if (live.telemetryCount.load() > 0)
                        return live.telemetry[0].load();
                }
            return -1.0f;
        };
        rack->host->shaperPatternNow = [&s, id] {
            osp::ShaperPattern pattern;
            for (int i = 0; i < 16; ++i)
            {
                const auto n = std::to_string (i + 1);
                auto& step = pattern[static_cast<std::size_t> (i)];
                step.start = static_cast<float> (s.document().paramValue (s.registry(), id, "s" + n));
                step.end = static_cast<float> (s.document().paramValue (s.registry(), id, "e" + n));
                step.shape = static_cast<osp::StepShape> (juce::jlimit (0, 5, juce::roundToInt (s.document().paramValue (s.registry(), id, "h" + n))));
            }
            return pattern;
        };
        rack->host->writeShaperPattern = [&s, id, host = rack->host.get()] (const osp::ShaperPattern& pattern) {
            host->beginAction();
            for (int i = 0; i < 16; ++i)
            {
                const auto n = std::to_string (i + 1);
                const auto& step = pattern[static_cast<std::size_t> (i)];
                const auto put = [&] (const std::string& param, double v) {
                    if (std::abs (s.document().paramValue (s.registry(), id, param) - v) > 1.0e-6)
                        s.setParam (id, param, v);
                };
                put ("s" + n, step.start);
                put ("e" + n, step.end);
                put ("h" + n, static_cast<double> (step.shape));
            }
        };
        return rack;
    }

    const std::map<std::string, Builder>& library()
    {
        static const std::map<std::string, Builder> racks { { "fx.echo", echo }, { "fx.space", space }, { "fx.movement", movement } };
        return racks;
    }
}

bool hasNativeRack (const std::string& type) { return library().count (type) > 0; }

std::unique_ptr<NativeRack> createNativeRack (Session& session, const std::string& nodeId)
{
    const auto* n = session.document().findNode (nodeId);
    if (n == nullptr)
        return nullptr;
    const auto it = library().find (n->type);
    return it != library().end() ? it->second (session, nodeId) : nullptr;
}

} // namespace andor::modular::ui
