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
    std::vector<std::string> placed;     ///< tiles the musician just put somewhere: they stay, others make room (resolveOverlaps)
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

/** A tile's face on the board, world px (what the editor draws: 184 wide, as tall as its
    ports, knobs and glass need). A module's or a collapsed group's. */
struct TileRect
{
    float x = 0.0f, y = 0.0f, w = 184.0f, h = 100.0f;
};
TileRect tileRect (const Document& doc, const Registry& reg, const std::string& nodeOrGroup);

/** The board's one layout rule: NO TILE OVERLAPS ANOTHER, ever. Moves tiles of the view that
    is open (doc.view.currentGroup) the least it can so that every pair keeps `clearanceX` /
    `clearanceY` px between their faces. `anchors` (what was just added, inserted or dropped)
    stay exactly where they are and the others make room. Deterministic, on the 8 px grid, and
    idempotent: a second pass moves nothing. Never touches a cable, a group's members' group, or
    a view that is not open. Returns the ids it moved. */
std::vector<std::string> resolveOverlaps (Document& doc, const Registry& reg, const std::vector<std::string>& anchors = {}, float clearanceX = 32.0f,
                                          float clearanceY = 24.0f);

/** What dragging a cable does (the canvas uses this, not `connect`): several sources may meet
    in one processor, and the patch is routed so that works as the musician means it.

      - A further source into an audio input that already has one: connected and summed there
        (a filter hears the sum, one filter per voice; it is not copied for each source).
      - Two voice chains that DIFFERENT note modules play (each its own ARP) into one per-voice
        processor would become one voice pool: every note of either would play both. Instead
        the processor is moved after the voice mixes and runs ONCE, globally, on the combined
        audio; each chain keeps its own notes ("shared effect runs once").
      - Voice audio into a module that already runs globally: through a VOICE MIX of its own.
      - Audio that is already one sound for all notes (after a VOICE MIX, or a source that plays
        once) into a per-voice module. A module that shapes each voice (an AMP ENVELOPE, one
        with per-voice modulation, or one that only runs per voice) is placed in the voices
        behind that sound, just before their VOICE MIX, and the sound goes on to where the
        module led; any other (a FILTER, a DRIVE with nothing per voice attached) is moved to
        run once, on that sound. A VOICE MIX left with nothing to mix is removed.

    Everything else is `connect`, with its reasons when it refuses. One transaction. */
Result connectSmart (Document& doc, const Registry& reg, const std::string& from, const std::string& fromPort, const std::string& to,
                     const std::string& toPort);

enum class DropAction { layer, morph, modulate, replace };
/** A source dragged onto another: LAYER (both sound), MORPH (a two-input MORPH, both wired),
    MODULATE (dragged into target's FM), REPLACE (target becomes dragged's type). */
Result dropOnSource (Document& doc, const Registry& reg, const std::string& dragged, const std::string& target, DropAction action);

/** The kinds of sound a SOURCE can be (one concept for the musician; each is one of the
    existing source modules underneath, with its own DSP, ports and where it runs). In the
    order they are offered. */
struct SourceType
{
    const char* type;    ///< the module type id ("osc.analog")
    const char* name;    ///< "ANALOG"
    const char* about;   ///< one line for a menu
};
const std::vector<SourceType>& sourceTypes();
/** "ANALOG", "ORGANIC"... for a source's module type ("" if it is not a source type). */
std::string sourceTypeName (const std::string& type);

/**
    Makes a sound source another kind of source, in place: the same module (its id, name,
    place, group, mute and solo, and so its letter and its card), another generator.

      - The type it leaves is kept on it as it was (its settings, its recording or plugin, where
        it ran, and the cables and macro / host assignments that only that type has): coming
        back to it brings all of that back.
      - Every cable and assignment the new type can honour stays. One it cannot (FM into a
        recording, a per-voice filter after a loop that plays once for all notes) is set aside
        with the old type, never silently lost.
      - The source stays in the sound: if its audio output's cable could not stay, it is
        connected again by the same rules as a dragged cable (through a VOICE MIX of its own
        into a global module), or, for a source that plays once for all notes, to where its
        chain's voices meet. A type that needs notes and has no notes cable gets the keyboard's.
      - If that cannot be done, or the patch would break a limit (a second hosted instrument),
        nothing changes and the reason is returned.

    One transaction. Structural: the patch is compiled again (the caller's swap is click-free). */
Result changeSourceType (Document& doc, const Registry& reg, const std::string& node, const std::string& type);

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
