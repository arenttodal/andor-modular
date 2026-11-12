#pragma once

#include "RackHost.h"

#include "EffectRack.h"

namespace andor::modular::ui
{

/** A module's rack as the family builds it: the component (an osp::plugin::rack::Module: the
    214 px device with its faceplate, rail, cells and graph) and the host that binds it to that
    one module. The host outlives the module. */
struct NativeRack
{
    std::unique_ptr<RackHost> host;
    std::function<std::unique_ptr<osp::plugin::rack::Module>()> make;
    /** How much of it is the family's own code (docs/ADDENDUM_02_REUSE_AUDIT.md):
        "shared": the very class ANDOR OSP and Analog show; "parts": built here from the
        family's rack parts, faceplate and drawing for a module they have no rack for. */
    juce::String provenance;
    /** What of the patch, besides the module's own settings, the rack was built from (its
        incoming modulations): when this changes the rack is built again. */
    std::function<std::string()> shape;
    std::string shapeAtBuild;
};

/** The rack library: the rack for `nodeId`'s type (and mode), or null where the module has no
    native rack yet (the drawer then shows the plain one). One entry per type: racks::library. */
std::unique_ptr<NativeRack> createNativeRack (Session& session, const std::string& nodeId);
/** True when `type` has a native rack. */
bool hasNativeRack (const std::string& type);

} // namespace andor::modular::ui
