#pragma once

#include "Racks.h"
#include "Theme.h"

namespace andor::modular::ui
{

/** A module's picture on its rack's glass: drawn from its parameters and from what the running
    graph reports about it (never an animation of its own). */
using GraphPainter = std::function<void (juce::Graphics&, juce::Rectangle<float> area, Session&, const std::string& nodeId, const ModuleLook&)>;

/** What a rack built from the family's parts shows of a module (see createModuleRack). */
struct ModuleRackSpec
{
    juce::String subtitle;                      ///< on the rail, after the name ("VOICE FILTER")
    std::string selector;                       ///< the choice in the selector row (its mode); "": the module's first quick choice
    std::vector<std::string> first;             ///< parameters shown first, in this order (the rest follow in the module's own)
    /** Named pages (their parameters by ID), where a module's controls fall into parts of
        their own (a mixer's levels, pans, mutes). Empty: as many rows as it takes, evenly. */
    std::vector<std::pair<juce::String, std::vector<std::string>>> pages;
    /** Parameters the rack edits as a picture, not as cells (the sequencer's steps). */
    std::function<bool (const ParamDesc&)> skip;
    GraphPainter graph;                         ///< "": the module's live display (paintModuleDisplay)
    float share = 0.64f;                        ///< the controls' share of the width (the graph takes the rest)
    /** A component of the module's own over the glass (the step sequencer's editor): made once. */
    std::function<std::unique_ptr<juce::Component> (Session&, const std::string& nodeId)> overlay;
};

/**
    A rack for a module that ANDOR OSP and Analog have no rack class for, built from the same
    parts as theirs (osp::plugin::rack::ParameterRack: the hardware faceplate with its rail, the
    selector row, one row of cells on the family's lines, pages when a row is not enough, the
    graph on its glass), in the module's own identity (Analog's device enamel and motif for
    processors and effects; a source's layer colour; the arpeggiator's amber for note modules;
    the modulation accent for modulators), with every control the module has in its mode and a
    picture that is specific to what it does (racks::spec).
*/
std::unique_ptr<NativeRack> createModuleRack (Session& session, const std::string& nodeId);

} // namespace andor::modular::ui
