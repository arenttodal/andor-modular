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

    const std::map<std::string, Builder>& library()
    {
        static const std::map<std::string, Builder> racks { { "fx.echo", echo } };
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
