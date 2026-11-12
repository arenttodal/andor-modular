#pragma once

#include "graph/Document.h"
#include "graph/Node.h"

#include "FxIdentity.h"        // ANDOR Analog: the devices' enamel identities and motifs
#include "OspLookAndFeel.h"    // ANDOR OSP: palette, fonts, type roles, the look-and-feel, the skin, the tokens

#include <juce_gui_basics/juce_gui_basics.h>

/**
    ANDOR Modular's interface is the ANDOR interface (andor-design-system/guide): the same
    tokens, type, look-and-feel, rendered ceramic and ivory, composed from the family's own code
    (shared/andor-ui) and assets, in ANDOR Analog's walnut finish. Nothing here defines a new
    colour, font, knob, key, texture or radius: this file only *names* what Modular uses and maps
    its modules onto the family's existing vocabulary (docs/UI_COMPONENTS.md):

      a sound source      -> a source card in miniature (cream ceramic, a layer badge, a graphite display)
      a processor / FX    -> its device's enamel and motif (Analog's patterned hardware, FxIdentity)
      a modulator         -> the modulation panel's cream face, the live accent
      a note module       -> the arpeggiator's key and amber
      the output          -> the routing board's OUTPUT terminal
*/
namespace andor::modular::ui
{

namespace design = osp::plugin::design;
namespace colour = osp::plugin::design::colour;
namespace draw = osp::plugin::design::draw;
namespace palette = osp::plugin::palette;
namespace fonts = osp::plugin::fonts;
namespace type = osp::plugin::type;
namespace skin = osp::plugin::skin;
using LookAndFeel = osp::plugin::OspLookAndFeel;
using Weight = osp::plugin::fonts::Weight;

/** ANDOR Modular's materials: ANDOR Analog's walnut housing and aged copper, the family's bone
    ceramic. Applied once per process, before any window (design::useMaterials). */
void applyMaterials();

/** The routing board's inks and lines (ANDOR Analog's ROUTING view, FxRouting.cpp): Modular's
    patch board is that board, grown. */
namespace board
{
    const juce::Colour faceTop { 0xfff9f5ee }, faceBottom { 0xfff2ece3 }, edge { 0xffd3cabe }, rule { 0xffe5dbcf }, column { 0xffd9d0c5 };
    const juce::Colour ink { 0xff313532 }, soft { 0xff716d65 }, weak { 0xff9c9489 };
    const juce::Colour wire { 0xffa3978a }, wireControl { 0xff9fb3a8 }, arrow { 0xffa59a8e }, sage { 0xff8caaa0 }, green { 0xff658276 };
    const juce::Colour terminalFace { 0xfffaf6ee }, terminalEdge { 0xffbfb4a6 }, terminalInk { 0xff5d5750 };
    const juce::Colour bone { 0xfff5eddf }, carbon { 0xff241d17 };   ///< the enamels' two inks
}

/** Reference pixels: ANDOR's 1448 x 1086 canvas (design::layout), scaled as one piece. */
namespace layout
{
    using R = juce::Rectangle<float>;
    constexpr float width = design::width, height = design::height;
    const R presetBar { 575.0f, 45.0f, 330.0f, 48.0f };
    const R saveKey { 913.0f, 45.0f, 44.0f, 48.0f };
    const R viewKey { 965.0f, 45.0f, 44.0f, 48.0f };
    const R volume = design::layout::volume;
    const R menu = design::layout::menu;
    const R board = design::layout::sources;
    const R macroPanel = design::layout::macroPanel;
    constexpr float macroFirstX = 112.0f, macroPitch = 138.0f, macroNameY = 719.0f, macroKnobY = 818.0f, macroKnob = 132.0f;
    constexpr float sceneDividerX = 1178.0f;
    const R scenes { 1192.0f, 696.0f, 208.0f, 190.0f };
    const R pitchWheel = design::layout::pitchWheel;
    const R keyboard { 97.0f, 913.0f, 1201.0f, 101.0f };
    const R voicesCard = design::layout::arpControl;
    const R evolveKey = design::layout::advanced;
    const R status { 48.0f, 1038.0f, 1250.0f, 24.0f };
    constexpr float boardHeader = 46.0f;
}

/** A module's tile on the board (reference px at zoom 1). */
namespace tile
{
    constexpr int pad = 8;            ///< room around the face for its shadow, sockets and selection ring
    constexpr int width = 184;
    constexpr int top = 42;           ///< the name band
    constexpr int portRow = 17;
    constexpr int modeRow = 20;
    constexpr int knobRow = 48;
    constexpr int glass = 24;
    constexpr float radius = 8.0f;
}

//==============================================================================
/** How a module is dressed (see the note at the top). */
struct ModuleLook
{
    enum class Face { enamel, card, panel, key, terminal, stack };
    Face face = Face::card;
    const andor::analog::FxVisualIdentity* id = nullptr;   ///< enamel
    juce::Colour ink, mutedInk, arc, edge;                 ///< name, captions, knob arcs and motif, the 1 px edge
    juce::Colour glassTop, glassBottom, trace;             ///< its display and the line on it
    int badge = -1;                                        ///< sources: the layer identity (0 A, 1 B, 2 C)
    juce::String badgeLetter;
};
/** `sourceOrdinal`: the source's place among the patch's sources (its badge A..F), else -1. */
ModuleLook lookFor (const std::string& type, int sourceOrdinal);
ModuleLook groupLook();

/** The tile's face in `r` (its shadow falls outside: leave tile::pad). */
void paintFace (juce::Graphics&, juce::Rectangle<float> r, const ModuleLook&, bool hover, bool bypassed);
/** The device's glass (a small display on its face). */
void paintGlass (juce::Graphics&, juce::Rectangle<float> r, const ModuleLook&);
/** A source's layer badge with its letter. */
void paintBadge (juce::Graphics&, juce::Rectangle<float> r, int identity, const juce::String& letter);
/** A status light on a face (the module's real ON). */
void paintLight (juce::Graphics&, juce::Rectangle<float> r, const ModuleLook&, bool on);

/** A port's socket: a bone disc on the tile's edge with the kind's glyph (shape and colour:
    notes a triangle in the arpeggiator's amber, audio a dot (stereo: ringed), control a
    diamond in moss, pitch a barred diamond in mineral blue, gate / trigger / clock a square
    in verdigris). */
juce::Colour portColour (PortKind) noexcept;
void drawSocket (juce::Graphics&, juce::Point<float> centre, PortKind, bool connected, bool hot, float size = 11.0f);

/** The eight macros wear the family's six identities in its on-screen order (rust, ochre,
    moss, mineral blue, verdigris, spore), then rust and ochre again. */
int macroIdentity (int slot) noexcept;
juce::Colour macroColour (int slot);
const char* macroLens (int slot) noexcept;   ///< the rendered lens' name ("life", "drive", ...)

/** A popover's raised cream card (the compact MiniPanel). */
void paintPopover (juce::Graphics&, juce::Rectangle<float> r, float radius = 14.0f);
/** The routing board's face. */
void paintBoard (juce::Graphics&, juce::Rectangle<float> r, float radius = 13.0f);
/** An arrowhead on a wire, pointing along `angle` (radians). */
void arrowAt (juce::Graphics&, juce::Point<float> tip, float angle, float size);

/** The header's thin-line icons (one stroke, one ink). */
namespace icon
{
    enum class Kind { cassette, patch, performance, previous, next, down, dots, undo, redo, plus, close, arrange, fit, chevron };
    void draw (juce::Graphics&, Kind, juce::Rectangle<float> area, juce::Colour ink, float stroke = 1.5f);
}

} // namespace andor::modular::ui
