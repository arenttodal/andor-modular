#pragma once

#include "core/Json.h"
#include "graph/Types.h"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace andor::modular
{

class Registry;
struct NodeDesc;

/**
    The editable patch (message thread). Everything audible and everything a musician built is
    here, by stable ids; the compiled graph is derived from it and never edited.
    docs/STATE_SCHEMA.md describes the saved form ("andormodular" JSON, schemaVersion 2).
*/

/** What a sound source keeps of a type it is not playing at the moment (edit::changeSourceType):
    that type's own settings and recording, where it ran, and the cables and macro or host
    assignments that only made sense for it. Switching back brings them back. */
struct ParkedCable
{
    std::string from, fromPort, to, toPort;
    bool toParam = false;
    double depth = 1.0;
    int transpose = 0;
};
struct ParkedMapping
{
    int slot = 0;                       ///< macro index, or host slot when `host`
    bool host = false;
    std::string param;
    double min = 0.0, max = 1.0;
    int curve = 0;                      ///< MacroCurve
};
struct SourceMode
{
    Domain domain = Domain::global;
    std::map<std::string, double> params;
    std::map<std::string, std::string> strings;
    std::vector<ParkedCable> cables;
    std::vector<ParkedMapping> mappings;
};

struct NodeModel
{
    std::string id;                     ///< stable UUID-like id ("n-1a2b3c4d")
    std::string type;                   ///< NodeDesc::type
    int typeVersion = 1;
    Domain domain = Domain::global;
    float x = 0.0f, y = 0.0f;           ///< canvas world coordinates (bounded +-100000)
    int size = 1;                       ///< 0 compact, 1 normal, 2 expanded
    bool enabled = true;                ///< off: bypassed where meaningful, else silent
    std::string label;                  ///< the musician's name for it (empty: the type's name)
    std::string group;                  ///< the group it belongs to ("" top level)
    std::map<std::string, double> params;        ///< native values by ParamDesc::id
    std::map<std::string, std::string> strings;  ///< e.g. "sample" -> asset key
    /** Sound sources: the musician's M and S. Not the same as `enabled` (a bypass): a muted or
        un-soloed source keeps running and keeps its routing; only what it adds to the sound is
        faded out at its own output (audibleGain). Saved only when set. */
    bool muted = false, solo = false;
    /** A sound source's other types, as it left them (by node type id). Saved only when any. */
    std::map<std::string, SourceMode> modes;

    double param (const std::string& key, double fallback) const
    {
        const auto it = params.find (key);
        return it != params.end() ? it->second : fallback;
    }
};

struct Connection
{
    std::string id;
    std::string from, fromPort;
    std::string to, toPort;             ///< toPort: an input port id, or a parameter id when toParam
    bool toParam = false;               ///< a modulation route into a parameter
    double depth = 1.0;                 ///< modulation depth -1..1 (normalised units)
    int transpose = 0;                  ///< notes edges: semitones added on the way
};

enum class MacroCurve : std::uint8_t { linear, inverted, logarithmic, exponential, bipolar };

struct MacroMapping
{
    std::string node;                   ///< node id, or a group id for a group control
    std::string param;                  ///< parameter id, or "control.N" of a group
    double min = 0.0, max = 1.0;        ///< normalised range of the target
    MacroCurve curve = MacroCurve::linear;
};

struct Macro
{
    std::string name;
    double value = 0.0, defaultValue = 0.0;   ///< 0..1
    int color = 0;                      ///< index into the ANDOR macro palette
    double smoothSeconds = 0.03;
    std::vector<MacroMapping> mappings;
    bool used() const noexcept { return ! mappings.empty(); }
};

struct ExposedPort
{
    std::string name;
    std::string node, port;
    bool input = true;
};

struct GroupModel
{
    std::string id;
    std::string name;
    std::string parent;                 ///< "" top level (one nested level shown in the prototype)
    float x = 0.0f, y = 0.0f;
    bool collapsed = true;
    std::vector<ExposedPort> ports;
    std::vector<Macro> controls;        ///< the group's own front-panel controls (<= 8)
    std::string module;                 ///< the .andormodule it came from (name), "" if made here
    bool muted = false, solo = false;   ///< M and S for every source inside it (theirs are kept)
};

struct Scene
{
    std::string name;
    bool captured = false;
    std::map<std::string, double> values;     ///< "nodeId/paramId" -> native value
    std::array<double, maxMacros> macros {};
};

struct HostBinding
{
    std::string node, param;            ///< empty: unbound
};

struct PatchSettings
{
    int polyphony = 8;                  ///< 1 / 2 / 4 / 8 / 16
    int voiceMode = 0;                  ///< 0 poly, 1 mono, 2 legato
    double glideSeconds = 0.0;
    std::uint64_t seed = 1;
    double masterDb = 0.0;
    double bendRange = 2.0;             ///< semitones
    bool limiter = true;                ///< output safety limiter
};

struct ViewState
{
    double zoom = 1.0, panX = 0.0, panY = 0.0;
    std::string currentGroup;           ///< the group being edited ("" top level)
    bool playView = false;
    int panelLayout = 0;                ///< 0 one row of 8, 1 two rows of 4
    std::string visualisation = "scope";
};

struct EvolveState
{
    std::uint64_t seed = 1;
    int generation = 0;
};

struct Document
{
    static constexpr int schemaVersion = 2;
    static constexpr const char* format = "andormodular";

    std::string name = "Untitled";
    std::string author;
    std::string description;
    PatchSettings settings;
    std::vector<NodeModel> nodes;
    std::vector<Connection> connections;
    std::vector<GroupModel> groups;
    std::array<Macro, maxMacros> macros;
    std::array<Scene, sceneCount> scenes;
    double scenePosition = 0.0;         ///< 0..3 (I..IV)
    int currentScene = 0;
    std::array<HostBinding, hostBankSize> hostBindings;
    ViewState view;
    EvolveState evolve;
    std::uint64_t idCounter = 1;        ///< deterministic ids

    // lookups
    NodeModel* findNode (const std::string& id) noexcept;
    const NodeModel* findNode (const std::string& id) const noexcept;
    Connection* findConnection (const std::string& id) noexcept;
    GroupModel* findGroup (const std::string& id) noexcept;
    const GroupModel* findGroup (const std::string& id) const noexcept;
    int nodeIndex (const std::string& id) const noexcept;

    /** A new id unique in this document ("n-0001a" style; deterministic from idCounter). */
    std::string makeId (const char* prefix);

    /** Adds a node of `type` at (x, y) with the descriptor's defaults. Null if unknown. */
    NodeModel* addNode (const Registry& registry, const std::string& type, float x, float y);
    void removeNode (const std::string& id);   ///< and its connections, mappings, bindings
    Connection& connect (const std::string& from, const std::string& fromPort, const std::string& to, const std::string& toPort);
    Connection& modulate (const std::string& from, const std::string& fromPort, const std::string& to, const std::string& param, double depth);
    void disconnect (const std::string& connectionId);

    /** Every native parameter value, defaults filled in (missing keys get the descriptor's default). */
    double paramValue (const Registry& registry, const std::string& node, const std::string& param) const;

    /** Every sample/plugin this patch references (the dependency manifest). */
    std::vector<std::string> assetKeys() const;

    std::string displayName (const Registry& registry, const NodeModel& node) const;
};

//==============================================================================
/** What a node adds to the sound under the patch's mutes and solos, 0 or 1 (the engine ramps
    it). Sources only (everything else: 1). Muted, itself or through a group it is in: 0. Else,
    while any source is soloed (itself or through its group): 1 for the soloed ones, 0 for the
    others. MUTE WINS over solo: a source someone muted never sounds by surprise. */
double audibleGain (const Document& doc, const Registry& registry, const NodeModel& node);
/** True while any source is soloed (itself or through a group). */
bool anySolo (const Document& doc, const Registry& registry);

// Persistence (deterministic JSON; migrations; fuzz-safe).

json::Value toJson (const Document& doc);
struct LoadResult
{
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;   ///< migrations, unknown types kept as missing modules
    int migratedFrom = 0;
};
LoadResult fromJson (const json::Value& v, Document& doc);

std::string saveToString (const Document& doc);
LoadResult loadFromString (const std::string& text, Document& doc);

/** Saves atomically: writes "<path>.tmp" then renames. */
bool saveToFile (const Document& doc, const std::string& path, std::string& error);
LoadResult loadFromFile (const std::string& path, Document& doc);

/** A short content hash (FNV-1a 64, hex) of text: asset ids, module hashes. */
std::string contentHash (const std::string& text);
std::string contentHash (const void* data, std::size_t size);

const char* macroCurveName (MacroCurve c) noexcept;
double applyMacroCurve (MacroCurve c, double x) noexcept;

} // namespace andor::modular
