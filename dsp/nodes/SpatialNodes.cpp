#include "nodes/NodeKit.h"

#include "engine/Arpeggiator.h"

namespace andor::modular
{
using namespace kit;

namespace
{
    constexpr double twoPi = 6.283185307179586;

    /**
        SATELLITE: a stereo bus set in motion. PAN sweeps it side to side (equal power); ORBIT
        circles it (side to side plus a "behind" that is quieter and darker: an honest stereo
        illusion, no binaural claims); BOUNCE ping-pongs it with eased turns. Free or on the
        host's beat; the motion's phase follows the host position when synced, so a bounce is
        the same every time. Level-based only (no inter-channel delay): its mono downmix stays
        coherent (no comb filter).
    */
    class Satellite final : public Processor
    {
    public:
        enum P { mode, rate, sync, division, depth, width, phaseOffset, manual };
        void prepare (const PrepareContext& pc) override { sr = pc.sampleRate; }
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float* p = io.params;
            const int m = static_cast<int> (p[mode]);
            const bool synced = p[sync] >= 0.5f;
            const double quarters = osp::arp::rateQuarters (static_cast<osp::ArpRate> (std::clamp (static_cast<int> (p[division]), 0, osp::arp::rateCount - 1))) * 8.0;
            const double spq = sr * 60.0 / std::max (1.0, ctx.bpm);
            const float d = p[depth], w = p[width];
            const double offset = p[phaseOffset];
            const float base = p[manual];
            float* l = io.out[0][0];
            float* r = io.out[0][1];
            float* posOut = io.out[1][0];
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                double ph;
                if (synced)
                {
                    const double c = (ctx.ppq + static_cast<double> (i) / spq) / quarters + offset;
                    ph = c - std::floor (c);
                }
                else
                {
                    phase += p[rate] / sr;
                    phase -= std::floor (phase);
                    const double c = phase + offset;
                    ph = c - std::floor (c);
                }
                float x = 0.0f, behind = 0.0f;
                switch (m)
                {
                    case 0: x = static_cast<float> (std::sin (twoPi * ph)); break;   // PAN
                    case 1:                                                          // ORBIT
                        x = static_cast<float> (std::sin (twoPi * ph));
                        behind = static_cast<float> (0.5 - 0.5 * std::cos (twoPi * ph));
                        break;
                    default:
                    {
                        // BOUNCE: a triangle with eased (smoothstep) turns
                        const double t = ph < 0.5 ? ph * 2.0 : 2.0 - ph * 2.0;
                        const double e = t * t * (3.0 - 2.0 * t);
                        x = static_cast<float> (2.0 * e - 1.0);
                        break;
                    }
                }
                const float pan = std::clamp (base + x * d, -1.0f, 1.0f);
                float gl, gr;
                panGains (pan, gl, gr);
                // Behind: up to -6 dB and a darker top (one-pole low-pass 18 kHz .. 3 kHz).
                const float gainBehind = 1.0f - 0.5f * behind * d;
                const float hz = 18000.0f - 15000.0f * behind * d;
                const float a = 1.0f - std::exp (-static_cast<float> (twoPi) * hz / static_cast<float> (sr));
                const float inL = io.in[0][0][i], inR = io.in[0][1][i];
                const float mid = 0.5f * (inL + inR), side = 0.5f * (inL - inR) * w;
                lowL += a * (mid - lowL);
                const float positioned = lowL * gainBehind;
                // The bus's own stereo (side) is kept (WIDTH), its centre placed.
                l[i] = 1.41421356f * gl * positioned + side;
                r[i] = 1.41421356f * gr * positioned - side;
                posOut[i] = pan;
            }
            lastPan = posOut[ctx.numSamples - 1];
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = lastPan;
            t.count = 1;
        }

    private:
        double sr = 48000.0, phase = 0.0;
        float lowL = 0.0f, lastPan = 0.0f;
    };

    class PanWidth final : public Processor
    {
    public:
        void process (const ChunkContext& ctx, ProcessIO& io) noexcept override
        {
            const float pan = std::clamp (io.params[0] + (io.inConnected[1] ? io.in[1][0][0] : 0.0f), -1.0f, 1.0f);
            const float width = io.params[1];
            float gl, gr;
            panGains (pan, gl, gr);
            gl *= 1.41421356f;
            gr *= 1.41421356f;
            const float a = gainL.next (gl, 0.01f, ctx), b = gainR.next (gr, 0.01f, ctx);
            for (int i = 0; i < ctx.numSamples; ++i)
            {
                const float l = io.in[0][0][i], r = io.in[0][1][i];
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
                io.out[0][0][i] = (mid + side) * a;
                io.out[0][1][i] = (mid - side) * b;
            }
        }

    private:
        Smoothed gainL, gainR;
    };
}

void registerSpatialNodes (Registry& r)
{
    {
        NodeDesc d;
        d.type = "spatial.satellite";
        d.name = "SATELLITE";
        d.description = "Sets a bus in motion: PAN, ORBIT, BOUNCE; free or on the beat";
        d.category = "Spatial";
        d.keywords = { "satellite", "pan", "orbit", "bounce", "auto pan", "motion", "space" };
        d.help = "A global stereo bus in motion. For notes that move on their own, give each voice an LFO into VOICE MIX's PAN input. "
                 "Level-based (no delay between the channels): the mono downmix stays clean.";
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.inputs = { in ("in", "IN", PortKind::audioStereo) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo), out ("pos", "POSITION", PortKind::control) };
        std::vector<std::string> rates;
        for (int i = 0; i < osp::arp::rateCount; ++i)
            rates.push_back (std::string ("x8 ") + osp::arp::rateName (static_cast<osp::ArpRate> (i)));
        d.params = { choice ("mode", "MODE", { "PAN", "ORBIT", "BOUNCE" }, 1, true), logNum ("rate", "SPEED", 0.02, 8.0, 0.25, "Hz", true),
                     toggle ("sync", "SYNC", false), choice ("division", "DIVISION", rates, 0), num ("depth", "DEPTH", 0.0, 1.0, 0.8, "%", true),
                     num ("width", "WIDTH", 0.0, 1.0, 1.0, "%"), num ("phase", "PHASE", 0.0, 1.0, 0.0, "%"), num ("manual", "PAN", -1.0, 1.0, 0.0, "", true) };
        d.cost.units = 0.2f;
        d.create = make<Satellite>();
        r.add (d);
    }
    {
        NodeDesc d;
        d.type = "spatial.pan";
        d.name = "PAN / WIDTH";
        d.description = "Places a stereo bus and sets its width (mid/side)";
        d.category = "Spatial";
        d.keywords = { "pan", "width", "stereo", "balance", "mid side" };
        d.domains = globalOnly;
        d.defaultDomain = Domain::global;
        d.inputs = { in ("in", "IN", PortKind::audioStereo), in ("pan", "PAN", PortKind::control) };
        d.outputs = { out ("out", "OUT", PortKind::audioStereo) };
        d.params = { num ("pan", "PAN", -1.0, 1.0, 0.0, "", true), num ("width", "WIDTH", 0.0, 2.0, 1.0, "%", true) };
        d.cost.units = 0.05f;
        d.create = make<PanWidth>();
        r.add (d);
    }
}

} // namespace andor::modular
