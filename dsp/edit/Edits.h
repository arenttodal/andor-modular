#pragma once

#include "graph/Document.h"
#include "graph/Registry.h"
#include "graph/Validator.h"

#include <string>
#include <vector>

/**
    Every edit a musician makes, as plain functions on a Document (message thread, headless,
    tested). Each smart action is one transaction: the caller snapshots the document into the
    History before it and the whole action undoes in one step. Every automatic choice is
    reported (Result::message) so the interface can show it ("Created MIXER") with its undo.
*/
namespace andor::modular::edit
{

struct Result
{
    bool ok = false;
    std::string message;                 ///< what happened (or why not), for people
    std::vector<std::string> created;    ///< node ids made by the action (first: the one asked for)
    std::string focus;                   ///< the node to select afterwards
    std::vector<std::string> choices;    ///< ambiguous placement: candidate connection ids
};

/** Snapshot undo/redo (documents are small: a few kB). */
class History
{
public:
    void push (const Document& before, std::string label);
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    std::string undoLabel() const { return undoStack.empty() ? std::string {} : undoStack.back().label; }
    std::string redoLabel() const { return redoStack.empty() ? std::string {} : redoStack.back().label; }
    bool undo (Document& doc);
    bool redo (Document& doc);
    void clear();
    static constexpr std::size_t depth = 200;

private:
    struct Entry
    {
        Document doc;
        std::string label;
    };
    std::vector<Entry> undoStack, redoStack;
};

// --- creation and smart wiring -------------------------------------------------------------

/** Adds a module the way Quick Add does. A sound source plays at once: KEYBOARD -> source ->
    AMP ENVELOPE -> VOICE MIX -> OUTPUT (each made only if missing). A second source joins the
    existing VOICE MIX (summed, never replacing the first); a global source joins the OUTPUT
    through a MIXER. Processors are inserted after `selected` when that is unambiguous. */
Result addModule (Document& doc, const Registry& reg, const std::string& type, float x, float y, const std::string& selected = {});

/** Sends a global stereo output to OUTPUT: directly when its input is free, else through a
    MIXER (joined or created). Returns the node it went into; `note` says what was made. */
std::string routeToOutput (Document& doc, const Registry& reg, const std::string& node, const std::string& port, std::vector<std::string>& created,
                           std::string& note);

/** A -> B becomes A -> P -> B in one step (P's first compatible input and output). */
Result insertOnConnection (Document& doc, const Registry& reg, const std::string& connectionId, const std::string& type);

/** Right-click on a port: a new module of `type` wired to that port. */
Result addConnectedTo (Document& doc, const Registry& reg, const std::string& node, const std::string& port, bool portIsOutput,
                       const std::string& type, float x, float y);

/** Module types with a port that can connect to this one (Quick Add from a port). */
std::vector<const NodeDesc*> compatibleTypes (const Document& doc, const Registry& reg, const std::string& node, const std::string& port, bool portIsOutput);

/** Connects with the validator's rules; on refusal returns its reason and suggestion. */
Result connect (Document& doc, const Registry& reg, const std::string& from, const std::string& fromPort, const std::string& to,
                const std::string& toPort);
Result modulate (Document& doc, const Registry& reg, const std::string& from, const std::string& fromPort, const std::string& to,
                 const std::string& param, double depth);

enum class DropAction { layer, morph, modulate, replace };
/** A source dragged onto another: LAYER (both sound), MORPH (a two-input MORPH, both wired),
    MODULATE (dragged into target's FM), REPLACE (target becomes dragged's type). */
Result dropOnSource (Document& doc, const Registry& reg, const std::string& dragged, const std::string& target, DropAction action);

/** Replaces a module by another type, keeping every cable whose port exists on the new one. */
Result replaceNode (Document& doc, const Registry& reg, const std::string& node, const std::string& type);

/** Copies (`withLinks`: cables from outside the selection too). Returns the new ids. */
Result duplicate (Document& doc, const Registry& reg, const std::vector<std::string>& nodes, bool withLinks, float dx = 40.0f, float dy = 40.0f);

void removeNodes (Document& doc, const std::vector<std::string>& nodes);

/** Left-to-right signal flow: notes and controllers, sources, processors, output. Positions
    of nodes listed in `keep` are left alone. */
void autoArrange (Document& doc, const Registry& reg, const std::vector<std::string>& keep = {});

// --- parameters, macros, scenes -------------------------------------------------------------

bool setParam (Document& doc, const Registry& reg, const std::string& node, const std::string& param, double native);
/** Maps a parameter (or a group control) to macro `index` over a normalised range. */
bool mapMacro (Document& doc, const Registry& reg, int index, const std::string& node, const std::string& param, double min, double max,
               MacroCurve curve = MacroCurve::linear);
/** Captures every parameter and macro into scene `index`. */
void captureScene (Document& doc, const Registry& reg, int index, const std::string& name = {});
/** Recalls a scene into the document (snap). */
bool recallScene (Document& doc, const Registry& reg, int index);
/** The interpolated values at a continuous position (0..3), written into the document. */
void applyScenePosition (Document& doc, const Registry& reg, double position);

// --- groups and modules (Groups.cpp) -----------------------------------------------------------

/** Puts nodes into a new group; cables crossing its edge become its named interface ports. */
Result group (Document& doc, const Registry& reg, const std::vector<std::string>& nodes, const std::string& name);
Result ungroup (Document& doc, const std::string& groupId);
/** Exposes a parameter as one of the group's own front-panel controls (<= 8). */
bool exposeControl (Document& doc, const Registry& reg, const std::string& groupId, const std::string& node, const std::string& param,
                    const std::string& name, double min = 0.0, double max = 1.0);
/** The group as a reusable .andormodule (its modules, inner cables, ports, controls). */
std::string exportModule (const Document& doc, const std::string& groupId);
/** Adds a saved module to the patch (fresh ids, its own runtime). Returns the new group id in `focus`. */
Result importModule (Document& doc, const Registry& reg, const std::string& moduleText, float x, float y);
/** Every node in a group and its sub-groups (flattened: budgets count these). */
std::vector<std::string> groupMembers (const Document& doc, const std::string& groupId);

// --- EVOLVE (Evolve.cpp) -------------------------------------------------------------------------

struct EvolveOptions
{
    enum class Intensity { subtle, moderate, wild };
    Intensity intensity = Intensity::subtle;
    std::uint64_t seed = 1;
    bool lockScale = true, lockRhythm = false, lockRouting = false, preserveLevel = true;
    std::vector<std::string> scope;     ///< nodes to change (empty: the whole patch)
    std::vector<std::string> protect;   ///< nodes never touched (a protected group)
    int maxAttempts = 8;
};

struct EvolveResult
{
    bool ok = false;
    Document doc;                        ///< the variation (the original is untouched)
    std::vector<std::string> changes;    ///< what was changed, for people
    int attempts = 0;
};

/** Rules-driven variation: the same document, options and seed always give the same result. */
EvolveResult evolve (const Document& doc, const Registry& reg, const EvolveOptions& options);

} // namespace andor::modular::edit
