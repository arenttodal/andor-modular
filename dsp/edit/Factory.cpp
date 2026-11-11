#include "edit/Factory.h"

#include "edit/Edits.h"

#include <stdexcept>

namespace andor::modular::factory
{

namespace
{
    /** Builds one patch; every step must succeed (a factory patch that cannot be built is a bug). */
    struct Builder
    {
        const Registry& reg;
        Document d;

        Builder (const Registry& r, const std::string& name, const std::string& description) : reg (r)
        {
            d.name = name;
            d.author = "ANDOR";
            d.description = description;
        }

        void check (const edit::Result& r, const char* what) const
        {
            if (! r.ok)
                throw std::runtime_error (d.name + ": " + what + ": " + r.message);
        }
        std::string add (const std::string& type, float x, float y, const std::string& selected = {})
        {
            const auto r = edit::addModule (d, reg, type, x, y, selected);
            check (r, type.c_str());
            return r.focus;
        }
        /** A bare module (no smart wiring). */
        std::string bare (const std::string& type, float x, float y, Domain domain)
        {
            auto* n = d.addNode (reg, type, x, y);
            if (n == nullptr)
                throw std::runtime_error ("unknown type " + type);
            n->domain = domain;
            return n->id;
        }
        void set (const std::string& node, const std::string& param, double v)
        {
            if (! edit::setParam (d, reg, node, param, v))
                throw std::runtime_error (d.name + ": no parameter " + param);
        }
        void sample (const std::string& node, const char* key) { d.findNode (node)->strings["sample"] = key; }
        void wire (const std::string& from, const std::string& fromPort, const std::string& to, const std::string& toPort)
        {
            check (edit::connect (d, reg, from, fromPort, to, toPort), "connect");
        }
        void mod (const std::string& from, const std::string& fromPort, const std::string& to, const std::string& param, double depth)
        {
            check (edit::modulate (d, reg, from, fromPort, to, param, depth), "modulate");
        }
        std::string cable (const std::string& from, const std::string& to) const
        {
            for (const auto& c : d.connections)
                if (c.from == from && c.to == to && ! c.toParam)
                    return c.id;
            throw std::runtime_error (d.name + ": no cable");
        }
        /** The module a node's main output feeds. */
        std::string after (const std::string& node) const
        {
            for (const auto& c : d.connections)
                if (c.from == node && ! c.toParam)
                    return c.to;
            throw std::runtime_error (d.name + ": nothing after");
        }
        std::string insert (const std::string& from, const std::string& to, const std::string& type)
        {
            const auto r = edit::insertOnConnection (d, reg, cable (from, to), type);
            check (r, type.c_str());
            return r.focus;
        }
        std::string first (const std::string& type) const
        {
            for (const auto& n : d.nodes)
                if (n.type == type)
                    return n.id;
            throw std::runtime_error (d.name + ": no " + type);
        }
        void macro (int index, const std::string& name, const std::string& node, const std::string& param, double min, double max,
                    MacroCurve curve = MacroCurve::linear)
        {
            if (! edit::mapMacro (d, reg, index, node, param, min, max, curve))
                throw std::runtime_error (d.name + ": macro " + name);
            d.macros[static_cast<std::size_t> (index)].name = name;
        }
        /** A second voice chain into its own VOICE MIX (independent from the first). */
        std::string ownVoiceMix (const std::string& vca, float x, float y)
        {
            d.disconnect (cable (vca, after (vca)));
            const auto vm = bare ("util.voicemix", x, y, Domain::global);
            wire (vca, "out", vm, "in");
            return vm;
        }
        void toOutput (const std::string& node)
        {
            std::vector<std::string> made;
            std::string note;
            if (edit::routeToOutput (d, reg, node, "out", made, note).empty())
                throw std::runtime_error (d.name + ": no route to OUTPUT");
        }
        Document finish()
        {
            edit::autoArrange (d, reg);
            Validator v (reg);
            const auto report = v.validate (d);
            if (report.hasErrors())
                throw std::runtime_error (d.name + ": invalid factory patch\n" + report.summary());
            return d;
        }
    };
}

std::vector<Document> patches (const Registry& reg)
{
    std::vector<Document> out;

    {   // 1. Basic Poly
        Builder b (reg, "Basic Poly", "One CLASSIC saw per note through the ladder filter, a filter envelope and a hall. The starting point.");
        const auto osc = b.add ("osc.analog", 0, 0);
        const auto vca = b.after (osc);
        const auto filter = b.insert (osc, vca, "proc.filter");
        const auto env = b.bare ("ctl.env", 0, 160, Domain::voice);
        b.wire (env, "out", filter, "env");
        b.set (filter, "cutoff", 900.0);
        b.set (filter, "envAmount", 0.45);
        b.set (env, "decay", 0.5);
        b.set (env, "sustain", 0.2);
        const auto vm = b.first ("util.voicemix");
        const auto space = b.insert (vm, b.after (vm), "fx.space");
        b.set (space, "mix", 0.22);
        b.macro (0, "CUTOFF", filter, "cutoff", 0.25, 0.85);
        b.macro (1, "RESONANCE", filter, "resonance", 0.0, 0.8);
        b.macro (2, "ENVELOPE", filter, "envAmount", 0.5, 0.95);
        b.macro (3, "ATTACK", vca, "attack", 0.0, 0.6);
        b.macro (4, "RELEASE", vca, "release", 0.2, 0.8);
        b.macro (5, "DRIFT", osc, "drift", 0.0, 1.0);
        b.macro (6, "SPACE", space, "mix", 0.0, 0.7);
        b.macro (7, "WIDTH", osc, "width", 0.2, 0.8);
        out.push_back (b.finish());
    }
    {   // 2. Analog + Organic Morph
        Builder b (reg, "Analog Organic Morph", "A DCO and a bowed recording of one note fused by MORPH (FUSE), then filtered per voice.");
        const auto osc = b.add ("osc.analog", 0, 0);
        b.set (osc, "model", 1);
        b.set (osc, "dcoWave", 2);
        b.set (osc, "sub", 0.3);
        const auto organic = b.add ("src.organic", 0, 200);
        b.sample (organic, bowedSample);
        const auto r = edit::dropOnSource (b.d, reg, organic, osc, edit::DropAction::morph);
        b.check (r, "morph");
        const auto morph = r.focus;
        const auto filter = b.insert (morph, b.after (morph), "proc.filter");
        b.set (filter, "mode", 1);
        b.set (filter, "cutoff", 3200.0);
        const auto vca = b.after (filter);
        b.set (vca, "attack", 0.08);
        b.set (vca, "release", 1.2);
        const auto vm = b.first ("util.voicemix");
        const auto space = b.insert (vm, b.after (vm), "fx.space");
        b.set (space, "mix", 0.3);
        b.macro (0, "MORPH", morph, "position", 0.0, 1.0);
        b.macro (1, "TEXTURE", morph, "texture", 0.0, 1.0);
        b.macro (2, "CUTOFF", filter, "cutoff", 0.3, 0.95);
        b.macro (3, "FORMANTS", morph, "formants", 0.25, 0.75);
        b.macro (4, "ATTACK", vca, "attack", 0.1, 0.7);
        b.macro (5, "RELEASE", vca, "release", 0.3, 0.9);
        b.macro (6, "SUB", osc, "sub", 0.0, 1.0);
        b.macro (7, "SPACE", space, "mix", 0.0, 0.7);
        out.push_back (b.finish());
    }
    {   // 3. Clockwork (clock-controlled pad)
        Builder b (reg, "Clockwork", "A wide pad breathing with a ticking clock loop at the host tempo: the clock's envelope opens its filter "
                                      "and its level; mute the clock and the groove stays.");
        const auto pad = b.add ("osc.analog", 0, 0);
        b.set (pad, "model", 2);
        b.set (pad, "wideWave", 4);
        b.set (pad, "spread", 0.5);
        const auto vca = b.after (pad);
        b.set (vca, "attack", 0.6);
        b.set (vca, "release", 2.0);
        b.set (vca, "sustain", 1.0);
        const auto vm = b.first ("util.voicemix");
        const auto outNode = b.after (vm);
        const auto filter = b.insert (vm, outNode, "proc.filter");
        b.set (filter, "cutoff", 600.0);
        b.set (filter, "envAmount", 0.6);
        const auto gain = b.insert (filter, outNode, "util.gain");
        const auto clock = b.add ("src.rhythm", -200, 300);
        b.sample (clock, clockSample);
        b.set (clock, "level", 0.6);
        b.wire (clock, "env", filter, "env");
        b.wire (clock, "env", gain, "cv");
        const auto mixer = b.first ("util.mixer");
        b.set (mixer, "level2", -12.0);
        b.macro (0, "GROOVE", filter, "envAmount", 0.5, 1.0);
        b.macro (1, "CUTOFF", filter, "cutoff", 0.2, 0.8);
        b.macro (2, "CLOCK LEVEL", mixer, "level2", 0.0, 0.9);
        b.macro (3, "SPREAD", pad, "spread", 0.0, 1.0);
        b.macro (4, "ATTACK", vca, "attack", 0.2, 0.8);
        b.macro (5, "RELEASE", vca, "release", 0.4, 0.9);
        out.push_back (b.finish());
    }
    {   // 4. Three Arps
        Builder b (reg, "Three Arps", "Three sources, three note streams: one sustains, one plays eighths, one runs up three octaves through "
                                      "a SATELLITE orbit. One keyboard.");
        const auto a = b.add ("osc.analog", 0, 0);
        b.set (a, "model", 2);
        b.set (a, "wideWave", 2);
        b.set (a, "octave", -1);
        const auto bOsc = b.add ("osc.analog", 0, 200);
        b.set (bOsc, "model", 1);
        b.set (bOsc, "dcoWave", 1);
        const auto arp2 = b.add ("event.arp", -150, 200, bOsc);
        b.set (arp2, "rate", 1);
        b.set (arp2, "pattern", 2);
        const auto c = b.add ("osc.analog", 0, 400);
        b.set (c, "wave", 2);
        b.set (c, "octave", 1);
        const auto arp3 = b.add ("event.arp", -150, 400, c);
        b.set (arp3, "rate", 2);
        b.set (arp3, "octaves", 3);
        b.set (arp3, "gate", 0.4);
        const auto vcaA = b.after (a);
        b.set (vcaA, "attack", 0.3);
        b.set (vcaA, "release", 1.5);
        b.set (b.after (bOsc), "decay", 0.25);
        b.set (b.after (bOsc), "sustain", 0.3);
        const auto vcaC = b.after (c);
        b.set (vcaC, "decay", 0.15);
        b.set (vcaC, "sustain", 0.0);
        const auto vm3 = b.ownVoiceMix (vcaC, 460, 400);
        const auto sat = b.bare ("spatial.satellite", 650, 400, Domain::global);
        b.wire (vm3, "out", sat, "in");
        b.set (sat, "sync", 1);
        b.toOutput (sat);
        const auto echo = b.insert (b.first ("util.mixer"), b.first ("util.output"), "fx.echo");
        b.set (echo, "mix", 0.25);
        b.macro (0, "ARP 2 RATE", arp2, "gate", 0.0, 1.0);
        b.macro (1, "ORBIT", sat, "depth", 0.0, 1.0);
        b.macro (2, "ORBIT SPEED", sat, "rate", 0.2, 0.8);
        b.macro (3, "ECHO", echo, "mix", 0.0, 0.8);
        out.push_back (b.finish());
    }
    {   // 5. Satellite Orchestra
        Builder b (reg, "Satellite Orchestra", "Three voices in three orbits (ORBIT, BOUNCE, PAN) around one room.");
        const auto a = b.add ("osc.analog", 0, 0);
        const auto bOsc = b.add ("osc.analog", 0, 200);
        b.set (bOsc, "model", 1);
        b.set (bOsc, "octave", 1);
        const auto c = b.add ("src.noise", 0, 400);
        b.set (c, "keytrack", 1.0);
        b.set (c, "tone", 0.35);
        const auto vm1 = b.first ("util.voicemix");
        const auto vm2 = b.ownVoiceMix (b.after (bOsc), 460, 200);
        const auto vm3 = b.ownVoiceMix (b.after (c), 460, 400);
        const auto s1 = b.insert (vm1, b.after (vm1), "spatial.satellite");
        const char* modes[] = { "ORBIT", "BOUNCE", "PAN" };
        (void) modes;
        b.set (s1, "mode", 1);
        int k = 0;
        for (const auto& vm : { vm2, vm3 })
        {
            const auto sat = b.bare ("spatial.satellite", 650, 200.0f + 200.0f * static_cast<float> (k), Domain::global);
            b.wire (vm, "out", sat, "in");
            b.set (sat, "mode", k == 0 ? 2 : 0);
            b.set (sat, "rate", k == 0 ? 0.4 : 0.11);
            b.toOutput (sat);
            ++k;
        }
        const auto space = b.insert (b.first ("util.mixer"), b.first ("util.output"), "fx.space");
        b.set (space, "mix", 0.35);
        b.macro (0, "ORBIT", s1, "depth", 0.0, 1.0);
        b.macro (1, "SPEED", s1, "rate", 0.1, 0.7);
        b.macro (2, "ROOM", space, "mix", 0.0, 0.8);
        out.push_back (b.finish());
    }
    {   // 6. Granular Rain
        Builder b (reg, "Granular Rain", "A rain recording as a global grain bed that follows the last note, and a bowed note in grains per voice, "
                                         "into ABYSS.");
        const auto bed = b.add ("src.granular", 0, 0);
        b.d.findNode (bed)->domain = Domain::voice;
        b.sample (bed, bowedSample);
        b.set (bed, "density", 18.0);
        b.set (bed, "size", 0.2);
        b.set (b.after (bed), "release", 2.5);
        const auto vm = b.first ("util.voicemix");
        const auto space = b.insert (vm, b.after (vm), "fx.space");
        b.set (space, "type", 4);
        b.set (space, "mix", 0.45);
        const auto lfo = b.add ("ctl.lfo", -100, 200, bed);
        b.set (lfo, "rate", 0.07);
        b.macro (0, "POSITION", bed, "position", 0.0, 1.0);
        b.macro (1, "DENSITY", bed, "density", 0.2, 0.9);
        b.macro (2, "SIZE", bed, "size", 0.2, 0.9);
        b.macro (3, "SPACE", space, "mix", 0.0, 0.9);
        out.push_back (b.finish());
    }
    {   // 7. Generative Sequence
        Builder b (reg, "Generative Sequence", "A seeded melody in D dorian for two oscillators an octave apart: the same seed plays the same notes, always.");
        const auto a = b.add ("osc.analog", 0, 0);
        const auto bOsc = b.add ("osc.analog", 0, 200);
        b.set (bOsc, "model", 2);
        b.set (bOsc, "wideWave", 2);
        const auto keys = b.first ("event.keyboard");
        const auto gen = b.bare ("event.generative", -300, 100, Domain::event);
        b.set (gen, "scale", 2);
        b.set (gen, "root", 2);
        // The generator plays both (B an octave up, on its cable).
        for (const auto& cid : std::vector<std::string> { b.cable (keys, a), b.cable (keys, bOsc) })
            b.d.disconnect (cid);
        b.wire (gen, "notes", a, "notes");
        b.wire (gen, "notes", bOsc, "notes");
        b.d.connections.back().transpose = 12;
        b.set (b.after (a), "decay", 0.3);
        b.set (b.after (a), "sustain", 0.2);
        b.set (b.after (bOsc), "decay", 0.5);
        b.set (b.after (bOsc), "sustain", 0.0);
        const auto vm = b.first ("util.voicemix");
        const auto echo = b.insert (vm, b.after (vm), "fx.echo");
        b.set (echo, "mix", 0.3);
        b.macro (0, "DENSITY", gen, "density", 0.1, 0.95);
        b.macro (1, "VARIATION", gen, "variation", 0.0, 0.01);
        b.macro (2, "STEPWISE", gen, "stepwise", 0.0, 1.0);
        b.macro (3, "ECHO", echo, "mix", 0.0, 0.7);
        out.push_back (b.finish());
    }
    {   // 8. Six Oscillator Stack
        Builder b (reg, "Six Oscillator Stack", "Six oscillators (all three models) per note: a huge detuned stack into one OTA filter and the ensemble chorus.");
        std::vector<std::string> oscs;
        for (int i = 0; i < 6; ++i)
        {
            const auto o = b.add ("osc.analog", 0, 150.0f * static_cast<float> (i));
            b.set (o, "model", i % 3);
            b.set (o, "fine", -12.0 + 5.0 * i);
            b.set (o, "level", 0.22);
            if (i == 5)
                b.set (o, "octave", -1);
            oscs.push_back (o);
        }
        const auto vm = b.first ("util.voicemix");
        b.set (vm, "level", 0.35);
        const auto filter = b.insert (vm, b.after (vm), "proc.filter");
        b.set (filter, "mode", 2);
        b.set (filter, "cutoff", 2800.0);
        const auto chorus = b.insert (filter, b.after (filter), "fx.chorus");
        b.set (chorus, "mode", 2);
        b.macro (0, "CUTOFF", filter, "cutoff", 0.3, 0.95);
        b.macro (1, "RESONANCE", filter, "resonance", 0.0, 0.7);
        out.push_back (b.finish());
    }
    {   // 9. Bowed Machine (a group with its own controls, also a factory module)
        Builder b (reg, "Bowed Machine", "A bowed recording morphed into a DCO and filtered per voice, grouped as one instrument with four controls.");
        const auto osc = b.add ("osc.analog", 0, 0);
        b.set (osc, "model", 1);
        b.set (osc, "dcoWave", 0);
        const auto organic = b.add ("src.organic", 0, 200);
        b.sample (organic, bowedSample);
        const auto r = edit::dropOnSource (b.d, reg, organic, osc, edit::DropAction::morph);
        b.check (r, "morph");
        const auto morph = r.focus;
        b.set (morph, "method", 0);
        b.set (morph, "position", 0.65);
        const auto filter = b.insert (morph, b.after (morph), "proc.filter");
        const auto vca = b.after (filter);
        b.set (vca, "attack", 0.15);
        b.set (vca, "release", 1.5);
        const auto g = edit::group (b.d, reg, { osc, organic, morph, filter, vca }, "BOWED MACHINE");
        b.check (g, "group");
        edit::exposeControl (b.d, reg, g.focus, morph, "position", "BOW", 0.0, 1.0);
        edit::exposeControl (b.d, reg, g.focus, morph, "texture", "RESIN", 0.0, 1.0);
        edit::exposeControl (b.d, reg, g.focus, filter, "cutoff", "TONE", 0.3, 0.95);
        edit::exposeControl (b.d, reg, g.focus, vca, "release", "RING", 0.2, 0.9);
        for (int k = 0; k < 4; ++k)
            b.macro (k, b.d.findGroup (g.focus)->controls[static_cast<std::size_t> (k)].name, g.focus, "control." + std::to_string (k), 0.0, 1.0);
        const auto vm = b.first ("util.voicemix");
        const auto space = b.insert (vm, b.after (vm), "fx.space");
        b.macro (4, "SPACE", space, "mix", 0.0, 0.8);
        out.push_back (b.finish());
    }
    {   // 10. Two-Morph Dream
        Builder b (reg, "Two-Morph Dream", "Two MORPHs: per voice (a saw into a bowed note) and global (that bus imprinted by a grain cloud), into ABYSS.");
        const auto osc = b.add ("osc.analog", 0, 0);
        const auto organic = b.add ("src.organic", 0, 200);
        b.sample (organic, bowedSample);
        const auto r = edit::dropOnSource (b.d, reg, organic, osc, edit::DropAction::morph);
        b.check (r, "morph");
        const auto vm = b.first ("util.voicemix");
        const auto outNode = b.after (vm);
        const auto cloud = b.bare ("src.granular", 0, 420, Domain::global);
        b.sample (cloud, rainSample);
        b.wire (b.first ("event.keyboard"), "notes", cloud, "notes");
        const auto gm = b.insert (vm, outNode, "proc.morph.global");
        b.wire (cloud, "out", gm, "b");
        b.set (gm, "position", 0.45);
        const auto space = b.insert (gm, outNode, "fx.space");
        b.set (space, "type", 4);
        b.set (space, "mix", 0.4);
        b.macro (0, "VOICE MORPH", r.focus, "position", 0.0, 1.0);
        b.macro (1, "BUS MORPH", gm, "position", 0.0, 1.0);
        b.macro (2, "CLOUD", cloud, "density", 0.1, 0.9);
        b.macro (3, "SPACE", space, "mix", 0.0, 0.9);
        out.push_back (b.finish());
    }
    {   // 11. Scene Performance
        Builder b (reg, "Scene Performance", "Four scenes from stillness to motion to fracture to afterglow; morph through them with the scene fader.");
        const auto osc = b.add ("osc.analog", 0, 0);
        b.set (osc, "model", 2);
        b.set (osc, "wideWave", 4);
        const auto vca = b.after (osc);
        const auto filter = b.insert (osc, vca, "proc.filter");
        const auto lfo = b.add ("ctl.lfo", -100, 200, filter);
        const auto vm = b.first ("util.voicemix");
        const auto outNode = b.after (vm);
        const auto sat = b.insert (vm, outNode, "spatial.satellite");
        const auto drive = b.insert (sat, outNode, "proc.drive");
        const auto echo = b.insert (drive, outNode, "fx.echo");
        const auto space = b.insert (echo, outNode, "fx.space");
        b.macro (0, "CUTOFF", filter, "cutoff", 0.2, 0.9);
        b.macro (1, "MOTION", lfo, "depth", 0.0, 1.0);
        b.macro (2, "ORBIT", sat, "depth", 0.0, 1.0);
        b.macro (3, "DRIVE", drive, "amount", 0.0, 1.0);
        b.macro (4, "ECHO", echo, "mix", 0.0, 0.8);
        b.macro (5, "SPACE", space, "mix", 0.0, 0.9);
        b.macro (6, "SPREAD", osc, "spread", 0.0, 1.0);
        b.macro (7, "RELEASE", vca, "release", 0.2, 0.9);
        const double scenes[4][8] = { { 0.35, 0.0, 0.0, 0.0, 0.0, 0.5, 0.1, 0.6 },
                                      { 0.6, 0.5, 0.6, 0.15, 0.3, 0.4, 0.5, 0.5 },
                                      { 0.85, 1.0, 1.0, 0.8, 0.6, 0.3, 0.9, 0.2 },
                                      { 0.3, 0.2, 0.3, 0.0, 0.5, 0.85, 0.4, 0.9 } };
        const char* names[4] = { "STILLNESS", "MOTION", "FRACTURE", "AFTERGLOW" };
        for (int s = 0; s < 4; ++s)
        {
            for (int m = 0; m < 8; ++m)
                b.d.macros[static_cast<std::size_t> (m)].value = scenes[s][m];
            b.set (lfo, "rate", 0.1 + 1.5 * s);
            b.set (sat, "mode", s % 3);
            edit::captureScene (b.d, reg, s, names[s]);
        }
        edit::recallScene (b.d, reg, 0);
        out.push_back (b.finish());
    }
    {   // 12. External Kontakt Demo
        Builder b (reg, "External Instrument Demo", "An installed instrument plugin (e.g. Kontakt strings) imprinted onto an analog carrier by GLOBAL "
                                                    "MORPH. Without the plugin the carrier plays alone (the module waits, its state kept).");
        const auto osc = b.add ("osc.analog", 0, 0);
        b.set (osc, "model", 2);
        b.set (b.after (osc), "attack", 0.2);
        b.set (b.after (osc), "release", 1.0);
        const auto ext = b.bare ("src.external", 0, 300, Domain::global);
        b.d.findNode (ext)->strings["plugin"] = "Kontakt 7";
        b.wire (b.first ("event.keyboard"), "notes", ext, "notes");
        const auto vm = b.first ("util.voicemix");
        const auto gm = b.insert (vm, b.after (vm), "proc.morph.global");
        b.wire (ext, "out", gm, "b");
        b.set (gm, "position", 0.5);
        b.macro (0, "IMPRINT", gm, "position", 0.0, 1.0);
        b.macro (1, "DETAIL", gm, "detail", 0.0, 1.0);
        out.push_back (b.finish());
    }
    return out;
}

std::vector<Module> modules (const Registry& reg)
{
    std::vector<Module> out;
    auto exportGroup = [&] (Builder& b, const std::vector<std::string>& nodes, const std::string& name, const std::vector<std::array<std::string, 3>>& controls) {
        const auto g = edit::group (b.d, reg, nodes, name);
        b.check (g, "group");
        for (const auto& c : controls)
            edit::exposeControl (b.d, reg, g.focus, c[0], c[1], c[2], 0.0, 1.0);
        out.push_back ({ name, edit::exportModule (b.d, g.focus) });
    };

    for (const auto& p : patches (reg))
        if (p.name == "Bowed Machine")
            for (const auto& g : p.groups)
                out.push_back ({ g.name, edit::exportModule (p, g.id) });
    {
        Builder b (reg, "Sub Bass Voice", "");
        const auto osc = b.add ("osc.analog", 0, 0);
        b.set (osc, "model", 1);
        b.set (osc, "sub", 0.8);
        b.set (osc, "octave", -1);
        const auto vca = b.after (osc);
        const auto f = b.insert (osc, vca, "proc.filter");
        b.set (f, "cutoff", 400.0);
        exportGroup (b, { osc, f, vca }, "SUB BASS VOICE", { { { f, "cutoff", "TONE" } }, { { osc, "sub", "SUB" } }, { { vca, "decay", "PUNCH" } } });
    }
    {
        Builder b (reg, "Pluck Voice", "");
        const auto osc = b.add ("osc.analog", 0, 0);
        b.set (osc, "wave", 2);
        const auto vca = b.after (osc);
        b.set (vca, "decay", 0.25);
        b.set (vca, "sustain", 0.0);
        b.set (vca, "release", 0.3);
        const auto f = b.insert (osc, vca, "proc.filter");
        const auto env = b.bare ("ctl.env", 0, 160, Domain::voice);
        b.set (env, "decay", 0.18);
        b.set (env, "sustain", 0.0);
        b.wire (env, "out", f, "env");
        b.set (f, "envAmount", 0.7);
        b.set (f, "cutoff", 500.0);
        exportGroup (b, { osc, f, vca, env }, "PLUCK VOICE", { { { f, "cutoff", "BRIGHT" } }, { { env, "decay", "SNAP" } }, { { vca, "decay", "LENGTH" } } });
    }
    {
        Builder b (reg, "Tape Space", "");
        const auto osc = b.add ("osc.analog", 0, 0);
        const auto vm = b.first ("util.voicemix");
        const auto echo = b.insert (vm, b.after (vm), "fx.echo");
        const auto space = b.insert (echo, b.after (echo), "fx.space");
        (void) osc;
        exportGroup (b, { echo, space }, "TAPE SPACE", { { { echo, "mix", "ECHO" } }, { { echo, "feedback", "REPEATS" } }, { { space, "mix", "ROOM" } } });
    }
    {
        Builder b (reg, "Orbit Bus", "");
        b.add ("osc.analog", 0, 0);
        const auto vm = b.first ("util.voicemix");
        const auto sat = b.insert (vm, b.after (vm), "spatial.satellite");
        const auto ch = b.insert (sat, b.after (sat), "fx.chorus");
        exportGroup (b, { sat, ch }, "ORBIT BUS", { { { sat, "depth", "ORBIT" } }, { { sat, "rate", "SPEED" } }, { { ch, "mix", "ENSEMBLE" } } });
    }
    {
        Builder b (reg, "Rhythm Gate", "");
        b.add ("osc.analog", 0, 0);
        const auto vm = b.first ("util.voicemix");
        const auto gain = b.insert (vm, b.after (vm), "util.gain");
        const auto lfo = b.bare ("ctl.lfo", 0, 200, Domain::global);
        b.set (lfo, "sync", 1);
        b.set (lfo, "shape", 4);
        b.set (lfo, "unipolar", 1);
        b.set (lfo, "division", 2);
        b.wire (lfo, "out", gain, "cv");
        exportGroup (b, { gain, lfo }, "RHYTHM GATE", { { { lfo, "depth", "DEPTH" } }, { { lfo, "shape", "SHAPE" } } });
    }
    return out;
}

} // namespace andor::modular::factory
