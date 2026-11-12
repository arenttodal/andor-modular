#include "Theme.h"

#include "ModularMaterialsData.h"

#include "fx/FxGraph.h"

namespace andor::modular::ui
{

using andor::analog::FxVisualIdentity;
namespace afx = andor::analog::fx;

void applyMaterials()
{
    static bool applied = false;
    if (applied)
        return;
    applied = true;
    // Modular's finish (docs/MATERIALS.md): ebonized ash cheeks, satin black anodized aluminium
    // rails and end caps, dark countersunk fasteners: its own housing pack (tools/
    // make_ebonized_ash_skin.py), in place of the shared teak. Only the trim is dark: the body
    // is the family's bone ceramic, and the interaction colours are unchanged (the live accent,
    // layer A's amber trace, the mineral macro identities with SPACE in spore), so modules,
    // sources and selected controls read as they did.
    auto m = design::Materials::osp();
    m.accent = juce::Colour (0xffc8703f);
    m.accentTop = juce::Colour (0xffd57e48);
    m.accentBottom = juce::Colour (0xffa9542b);
    m.accentSoft = juce::Colour (0xfff1cbb2);
    m.paletteAccent = juce::Colour (0xffcc7a48);
    m.layerA = juce::Colour (0xffd78b50);
    m.identityA = { juce::Colour (0xffd9824c), juce::Colour (0xff9c4a22), juce::Colour (0xfff0a060), juce::Colour (0xffc8703f), juce::Colour (0xffd57e48),
                    juce::Colour (0xffe4b07a) };
    m.graphiteOnWell = true;
    m.macros = { juce::Colour (0xffbe7552), juce::Colour (0xffb5843a), juce::Colour (0xff7b8d69),
                 juce::Colour (0xff73899c), juce::Colour (0xff8b9180), juce::Colour (0xff5c9490) };
    m.pack = ModularMaterialsData::ebonizedashskin_zip;
    m.packSize = static_cast<size_t> (ModularMaterialsData::ebonizedashskin_zipSize);
    design::useMaterials (m);
}

//==============================================================================
namespace
{
    ModuleLook enamel (const FxVisualIdentity& id)
    {
        ModuleLook l;
        l.face = ModuleLook::Face::enamel;
        l.id = &id;
        l.ink = id.text;
        l.mutedInk = id.mutedText;
        l.arc = id.motifInk;
        l.edge = id.edge;
        l.glassTop = id.glassTop;
        l.glassBottom = id.glassBottom;
        l.trace = id.statusActive;   // (the glass is graphite: the device's light, not its ink)
        return l;
    }

    ModuleLook cream (ModuleLook::Face face, juce::Colour arc, juce::Colour trace)
    {
        ModuleLook l;
        l.face = face;
        l.ink = colour::text;
        l.mutedInk = colour::textSecondary;
        l.arc = arc;
        l.edge = colour::hairline;
        l.glassTop = colour::wellA;
        l.glassBottom = colour::wellB;
        l.trace = trace;
        return l;
    }
}

ModuleLook lookFor (const std::string& type, int sourceOrdinal)
{
    using T = afx::Type;
    using andor::analog::visualIdentityFor;
    using andor::analog::synthIdentity;
    using andor::analog::SynthRack;
    struct Map
    {
        const char* type;
        T fx;
    };
    static const Map devices[] = { { "proc.drive", T::drive },       { "fx.movement", T::movement },     { "fx.echo", T::echo },
                                   { "fx.space", T::space },         { "fx.convolution", T::convolution }, { "fx.spread", T::spread },
                                   { "spatial.satellite", T::spread }, { "fx.compressor", T::compressor }, { "proc.tone", T::eq },
                                   { "fx.filter", T::filter },       { "fx.phaser", T::phaser },         { "fx.flanger", T::flanger },
                                   { "fx.chorus", T::flanger },      { "fx.shifter", T::shifter },       { "fx.retro", T::retro } };
    for (const auto& d : devices)
        if (type == d.type)
            return enamel (visualIdentityFor (d.fx));
    if (type == "proc.filter")
        return enamel (synthIdentity (SynthRack::voiceFilter));
    if (type == "proc.morph" || type == "proc.morph.global" || type == "proc.reimagined")
        return enamel (synthIdentity (SynthRack::morph));
    if (type == "util.output")
    {
        auto l = cream (ModuleLook::Face::terminal, colour::accent, colour::accent);
        l.ink = board::terminalInk;
        l.edge = board::terminalEdge;
        return l;
    }
    if (type.rfind ("event.", 0) == 0)
        return cream (ModuleLook::Face::key, colour::arp, colour::arpOnDark);
    if (type.rfind ("ctl.", 0) == 0 || type == "proc.vca")
        return cream (ModuleLook::Face::panel, colour::accent, colour::accent);
    if (type.rfind ("osc.", 0) == 0 || type.rfind ("src.", 0) == 0)
    {
        const int identity = sourceOrdinal < 0 ? 0 : sourceOrdinal % 3;
        const auto& idy = colour::identity (identity);
        auto l = cream (ModuleLook::Face::card, idy.arc, idy.wave);
        l.badge = identity;
        l.badgeLetter = juce::String::charToString (static_cast<juce::juce_wchar> ('A' + std::max (0, sourceOrdinal) % 26));
        return l;
    }
    // Mixers, gains, switches, meters, the voice mix, pan: the UTILITY device (a meter bridge).
    return enamel (visualIdentityFor (T::utility));
}

ModuleLook groupLook()
{
    // The routing board's group summary: a neutral graphite enamel card on a fanned stack.
    ModuleLook l;
    l.face = ModuleLook::Face::stack;
    l.ink = board::bone;
    l.mutedInk = board::bone.withAlpha (0.74f);
    l.arc = board::bone;
    l.edge = juce::Colour (0xff6d6962).darker (0.45f);
    l.glassTop = colour::wellA;
    l.glassBottom = colour::wellB;
    l.trace = board::bone;
    return l;
}

void paintFace (juce::Graphics& g, juce::Rectangle<float> r, const ModuleLook& look, bool hover, bool bypassed)
{
    const float radius = tile::radius;
    switch (look.face)
    {
        case ModuleLook::Face::enamel:
        {
            // The device in miniature (FxRoutingView::paintTile): its enamel, its motif in the
            // left quarter fading toward the name, a lit inner line, its edge.
            const auto& id = *look.id;
            g.setColour (juce::Colours::black.withAlpha (hover ? 0.20f : 0.15f));
            g.fillRoundedRectangle (r.translated (0.0f, hover ? 2.5f : 2.0f), radius);
            juce::Path face;
            face.addRoundedRectangle (r, radius);
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (face);
                g.setGradientFill (juce::ColourGradient (id.enamel.brighter (hover ? 0.10f : 0.06f), 0.0f, r.getY(), id.enamel.darker (0.07f), 0.0f, r.getBottom(), false));
                g.fillRect (r);
                const auto pattern = r.withWidth (46.0f).withHeight (static_cast<float> (tile::top));
                for (int i = -1; i < 6; ++i)
                {
                    const float solid = 0.6f * pattern.getWidth(), stepW = (pattern.getWidth() - solid + 10.0f) / 6.0f;
                    const auto strip = i < 0 ? pattern.withWidth (solid) : juce::Rectangle<float> (pattern.getX() + solid + stepW * static_cast<float> (i), r.getY(), stepW, pattern.getHeight());
                    juce::Graphics::ScopedSaveState s (g);
                    g.reduceClipRegion (strip.getSmallestIntegerContainer());
                    g.beginTransparencyLayer (i < 0 ? 1.0f : 1.0f - (static_cast<float> (i) + 0.5f) / 6.0f);
                    andor::analog::paintFxMotif (g, id.motif, pattern.withTrimmedTop (3.0f).withTrimmedBottom (3.0f).withWidth (pattern.getWidth() + 10.0f), id,
                                                 andor::analog::MotifScale::mini);
                    g.endTransparencyLayer();
                }
                if (bypassed)
                {
                    g.setColour (board::faceBottom.withAlpha (0.42f));
                    g.fillRect (r);
                }
            }
            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawRoundedRectangle (r.reduced (1.0f).withTrimmedBottom (1.0f), radius - 1.0f, 0.8f);
            g.setColour (id.edge.withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            break;
        }
        case ModuleLook::Face::stack:
        {
            const juce::Colour face (0xff6d6962);
            for (int layer = 2; layer >= 1; --layer)
            {
                const auto back = r.translated (3.0f * static_cast<float> (layer), -3.0f * static_cast<float> (layer));
                g.setColour (face.brighter (0.25f * static_cast<float> (layer)));
                g.fillRoundedRectangle (back, radius);
                g.setColour (face.darker (0.3f));
                g.drawRoundedRectangle (back.reduced (0.5f), radius, 1.0f);
            }
            g.setColour (juce::Colours::black.withAlpha (0.15f));
            g.fillRoundedRectangle (r.translated (0.0f, 2.0f), radius);
            g.setGradientFill (juce::ColourGradient (face.brighter (hover ? 0.12f : 0.06f), 0.0f, r.getY(), face.darker (0.08f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setColour (face.darker (0.45f));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            break;
        }
        case ModuleLook::Face::terminal:
            g.setColour (board::terminalFace);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (board::terminalEdge);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            break;
        case ModuleLook::Face::card:
            draw::raised (g, r, 9.0f, colour::cardTop, colour::cardBottom, 0.7f);
            break;
        case ModuleLook::Face::panel:
            draw::raised (g, r, 9.0f, colour::panelTop, colour::panelBottom, 0.7f);
            break;
        case ModuleLook::Face::key:
            draw::raised (g, r, 9.0f, colour::buttonTop, colour::buttonBottom, 0.7f);
            break;
    }
    if (bypassed && look.face != ModuleLook::Face::enamel && look.face != ModuleLook::Face::stack)
    {
        // Off: the face under the housing's own veil, still in its place (the disabled look).
        g.setColour (colour::housingBottom.withAlpha (0.55f));
        g.fillRoundedRectangle (r, radius);
    }
}

void paintGlass (juce::Graphics& g, juce::Rectangle<float> r, const ModuleLook& look)
{
    // A device's graph glass (the rack's): tinted by its enamel, a shade under the top edge.
    juce::Path shape;
    shape.addRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (look.face == ModuleLook::Face::enamel || look.face == ModuleLook::Face::stack ? 0.14f : 0.55f));
    g.strokePath (shape, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 1.0f));
    g.setGradientFill (juce::ColourGradient (look.glassTop, 0.0f, r.getY(), look.glassBottom, 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, r.getY(), juce::Colours::black.withAlpha (0.0f), 0.0f, r.getY() + 6.0f, false));
        g.fillRect (r.withHeight (6.0f));
    }
    g.setColour (colour::wellRim);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

void paintBadge (juce::Graphics& g, juce::Rectangle<float> r, int identity, const juce::String& letter)
{
    // The rendered badge (layer A's in the accent, from Modular's pack); the letter in white.
    static const char* names[] = { "badge-A", "badge-B", "badge-C" };
    const auto& idy = colour::identity (identity);
    bool drawn = false;
    if (auto* pieces = skin::active())
        drawn = pieces->draw (g, names[juce::jlimit (0, 2, identity)], r);
    if (! drawn)
    {
        g.setGradientFill (juce::ColourGradient (idy.badgeTop, 0.0f, r.getY(), idy.badgeBottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
    }
    g.setColour (juce::Colours::white);
    g.setFont (fonts::make (r.getHeight() * 0.56f, Weight::medium));
    g.drawText (letter, r, juce::Justification::centred, false);
}

void paintLight (juce::Graphics& g, juce::Rectangle<float> area, const ModuleLook& look, bool on)
{
    const auto light = area.withSizeKeepingCentre (8.5f, 8.5f);
    if (look.face == ModuleLook::Face::enamel || look.face == ModuleLook::Face::stack)
    {
        // The tile's status light (FxRoutingView::paintTile).
        const juce::Colour lit = look.id != nullptr ? look.id->statusActive : juce::Colour (0xfffff4dc);
        const juce::Colour body = look.id != nullptr ? look.id->enamel : juce::Colour (0xff6d6962);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillEllipse (light.expanded (2.0f));
        if (! on)
        {
            g.setColour (body.darker (0.45f));
            g.fillEllipse (light);
            g.setColour (look.mutedInk.withAlpha (0.5f));
            g.drawEllipse (light.reduced (0.5f), 1.0f);
            return;
        }
        g.setColour (lit.withAlpha (0.30f));
        g.fillEllipse (light.expanded (2.5f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, light.getX(), light.getY(), lit.darker (0.08f), light.getRight(), light.getBottom(), false));
        g.fillEllipse (light);
        return;
    }
    // On ceramic: the family's drawn light, in the face's own colour.
    draw::led (g, light.getCentre(), 8.0f, on ? look.arc.withMultipliedBrightness (1.15f) : look.arc.interpolatedWith (colour::panelBottom, 0.6f), on ? 0.8f : 0.0f);
}

//==============================================================================
juce::Colour portColour (PortKind k) noexcept
{
    switch (k)
    {
        case PortKind::notes: return colour::arp;
        case PortKind::audioMono:
        case PortKind::audioStereo: return colour::text;
        case PortKind::control: return colour::macro (colour::Macro::character);
        case PortKind::pitch: return colour::macro (colour::Macro::movement);
        case PortKind::gate:
        case PortKind::trigger:
        case PortKind::clock: return colour::macro (colour::Macro::echo);
    }
    return colour::text;
}

void drawSocket (juce::Graphics& g, juce::Point<float> c, PortKind kind, bool connected, bool hot, float size)
{
    const float r = 0.5f * size;
    if (hot)
    {
        g.setColour (colour::accent.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (size + 8.0f, size + 8.0f).withCentre (c));
    }
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c.translated (0.0f, 1.0f)));
    g.setGradientFill (juce::ColourGradient (colour::buttonTop, c.x, c.y - r, colour::buttonBottom, c.x, c.y + r, false));
    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c));
    g.setColour (hot ? colour::accent : colour::knobRim);
    g.drawEllipse (juce::Rectangle<float> (size, size).withCentre (c).reduced (0.5f), 1.0f);
    if (kind == PortKind::audioStereo)
        g.drawEllipse (juce::Rectangle<float> (size + 4.0f, size + 4.0f).withCentre (c).reduced (0.5f), 0.9f);

    const float s = size * 0.27f;
    juce::Path glyph;
    switch (kind)
    {
        case PortKind::audioMono:
        case PortKind::audioStereo: glyph.addEllipse (c.x - s, c.y - s, 2.0f * s, 2.0f * s); break;
        case PortKind::notes: glyph.addTriangle (c.x - s, c.y - s * 1.1f, c.x - s, c.y + s * 1.1f, c.x + s * 1.2f, c.y); break;
        case PortKind::control:
        case PortKind::pitch:
            glyph.startNewSubPath (c.x, c.y - s * 1.25f);
            glyph.lineTo (c.x + s * 1.25f, c.y);
            glyph.lineTo (c.x, c.y + s * 1.25f);
            glyph.lineTo (c.x - s * 1.25f, c.y);
            glyph.closeSubPath();
            break;
        case PortKind::gate:
        case PortKind::trigger:
        case PortKind::clock: glyph.addRectangle (c.x - s * 0.9f, c.y - s * 0.9f, 1.8f * s, 1.8f * s); break;
    }
    g.setColour (portColour (kind).withAlpha (connected ? 1.0f : 0.75f));
    if (connected)
        g.fillPath (glyph);
    else
        g.strokePath (glyph, juce::PathStrokeType (1.1f));
    if (kind == PortKind::pitch)
        g.fillRect (c.x - s * 1.25f, c.y + s * 1.6f, 2.5f * s, 0.9f);
}

//==============================================================================
int macroIdentity (int slot) noexcept
{
    // The family's on-screen order: LIFE DRIVE CHARACTER MOVEMENT ECHO SPACE, then again.
    static constexpr int order[8] { 0, 1, 2, 3, 5, 4, 0, 1 };
    return order[((slot % 8) + 8) % 8];
}

juce::Colour macroColour (int slot) { return colour::macro (macroIdentity (slot)); }

const char* macroLens (int slot) noexcept
{
    static const char* const names[] = { "life", "drive", "character", "motion", "space", "echo" };
    return names[macroIdentity (slot)];
}

void paintPopover (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    // MiniPanel (compact): warm cream, a hairline edge, a soft shadow, a lit top line.
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    design::CachedShadow (juce::Colour (0x3a281c10), 22, { 0, 8 }).drawForPath (g, shape);
    design::CachedShadow (juce::Colour (0x221e1a16), 3, { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f4ec), 0.0f, r.getY(), juce::Colour (0xffeee7db), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (0xffd3cabc));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawHorizontalLine (juce::roundToInt (r.getY() + 1.0f), r.getX() + radius, r.getRight() - radius);
}

void paintBoard (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    // FxRoutingView::paint: the board.
    g.setColour (juce::Colour (0x1d3c3225));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), radius);
    g.setGradientFill (juce::ColourGradient (board::faceTop, r.getX(), r.getY(), board::faceBottom, r.getRight(), r.getBottom(), false));
    g.fillRoundedRectangle (r.reduced (0.5f), radius);
    g.setColour (board::edge);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void arrowAt (juce::Graphics& g, juce::Point<float> tip, float angle, float size)
{
    juce::Path p;
    p.startNewSubPath (-size, -0.75f * size);
    p.lineTo (0.0f, 0.0f);
    p.lineTo (-size, 0.75f * size);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::rotation (angle).translated (tip.x, tip.y));
}

//==============================================================================
void icon::draw (juce::Graphics& g, Kind kind, juce::Rectangle<float> a, juce::Colour ink, float stroke)
{
    const auto c = a.getCentre();
    const float u = std::min (a.getWidth(), a.getHeight()) / 24.0f;   // drawn on a 24 unit grid
    auto pt = [&] (float x, float y) { return juce::Point<float> (c.x + (x - 12.0f) * u, c.y + (y - 12.0f) * u); };
    juce::Path p;
    const juce::PathStrokeType pen (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (ink);
    switch (kind)
    {
        case Kind::cassette:
            p.addRoundedRectangle (juce::Rectangle<float> (pt (3, 6), pt (21, 18)), 2.2f * u);
            p.addEllipse (juce::Rectangle<float> (pt (7, 9.5f), pt (10.6f, 13.1f)));
            p.addEllipse (juce::Rectangle<float> (pt (13.4f, 9.5f), pt (17, 13.1f)));
            p.startNewSubPath (pt (7.5f, 18));
            p.lineTo (pt (9, 15.2f));
            p.lineTo (pt (15, 15.2f));
            p.lineTo (pt (16.5f, 18));
            break;
        case Kind::patch:
            // three nodes and their two cables (the routing key)
            p.addEllipse (juce::Rectangle<float> (pt (4, 4.5f), pt (8, 8.5f)));
            p.addEllipse (juce::Rectangle<float> (pt (4, 15.5f), pt (8, 19.5f)));
            p.addEllipse (juce::Rectangle<float> (pt (16, 10), pt (20, 14)));
            p.startNewSubPath (pt (8, 6.5f));
            p.cubicTo (pt (12, 6.5f), pt (12, 12), pt (16, 12));
            p.startNewSubPath (pt (8, 17.5f));
            p.cubicTo (pt (12, 17.5f), pt (12, 12), pt (16, 12));
            break;
        case Kind::performance:
            // a knob: the front panel
            p.addEllipse (juce::Rectangle<float> (pt (5, 5), pt (19, 19)));
            p.startNewSubPath (pt (12, 12));
            p.lineTo (pt (16, 8));
            break;
        case Kind::previous:
            p.startNewSubPath (pt (14, 7));
            p.lineTo (pt (9.5f, 12));
            p.lineTo (pt (14, 17));
            break;
        case Kind::next:
        case Kind::chevron:
            p.startNewSubPath (pt (10, 7));
            p.lineTo (pt (14.5f, 12));
            p.lineTo (pt (10, 17));
            break;
        case Kind::down:
            p.startNewSubPath (pt (7.5f, 10));
            p.lineTo (pt (12, 14.5f));
            p.lineTo (pt (16.5f, 10));
            break;
        case Kind::dots:
            for (const float y : { 5.5f, 12.0f, 18.5f })
                g.fillEllipse (juce::Rectangle<float> (3.6f * u, 3.6f * u).withCentre (pt (12, y)));
            return;
        case Kind::undo:
        case Kind::redo:
        {
            const bool redo = kind == Kind::redo;
            auto m = [&] (float x, float y) { return pt (redo ? 24.0f - x : x, y); };
            p.startNewSubPath (m (9, 6));
            p.lineTo (m (5, 10));
            p.lineTo (m (9, 14));
            p.startNewSubPath (m (5, 10));
            p.lineTo (m (14, 10));
            p.cubicTo (m (20, 10), m (20, 18), m (14, 18));
            p.lineTo (m (10, 18));
            break;
        }
        case Kind::plus:
            p.startNewSubPath (pt (12, 6));
            p.lineTo (pt (12, 18));
            p.startNewSubPath (pt (6, 12));
            p.lineTo (pt (18, 12));
            break;
        case Kind::close:
            p.startNewSubPath (pt (7, 7));
            p.lineTo (pt (17, 17));
            p.startNewSubPath (pt (17, 7));
            p.lineTo (pt (7, 17));
            break;
        case Kind::arrange:
            p.addRoundedRectangle (juce::Rectangle<float> (pt (3, 8.5f), pt (9, 15.5f)), 1.5f * u);
            p.addRoundedRectangle (juce::Rectangle<float> (pt (15, 8.5f), pt (21, 15.5f)), 1.5f * u);
            p.startNewSubPath (pt (9, 12));
            p.lineTo (pt (15, 12));
            break;
        case Kind::fit:
            for (const auto& corner : { std::array<float, 6> { 5, 9, 5, 5, 9, 5 }, std::array<float, 6> { 15, 5, 19, 5, 19, 9 },
                                        std::array<float, 6> { 19, 15, 19, 19, 15, 19 }, std::array<float, 6> { 9, 19, 5, 19, 5, 15 } })
            {
                p.startNewSubPath (pt (corner[0], corner[1]));
                p.lineTo (pt (corner[2], corner[3]));
                p.lineTo (pt (corner[4], corner[5]));
            }
            break;
    }
    g.strokePath (p, pen);
}

} // namespace andor::modular::ui
