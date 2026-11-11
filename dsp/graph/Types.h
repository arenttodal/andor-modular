#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace andor::modular
{

/** Block structure: the engine renders every host block in chunks of at most this many
    samples, split also at every note event (sample-accurate onsets). Control-rate signals are
    resolved once per chunk (so the control rate is at least sampleRate / 32: 1.5 kHz at 48 kHz);
    audio-rate inputs (FM, ring, sync) read every sample. docs/DSP_CONTRACTS.md. */
constexpr int maxChunk = 32;
constexpr int maxVoices = 16;
constexpr int maxPorts = 8;          ///< inputs or outputs per node
constexpr int maxMacros = 8;
constexpr int sceneCount = 4;
constexpr int hostBankSize = 32;     ///< fixed host-automation slots bound to node parameters

/** What a port carries. Every kind has an explicit contract (docs/PORT_TYPES.md). */
enum class PortKind : std::uint8_t
{
    notes,        ///< note events: on/off, velocity, note id, sample offset
    pitch,        ///< control-rate pitch offset in semitones (unit: st)
    gate,         ///< 0 / 1 held level
    trigger,      ///< single-sample 1 pulses
    control,      ///< normalised control: unipolar 0..1 or bipolar -1..1 (see Polarity)
    audioMono,
    audioStereo,
    clock         ///< pulses at a musical division (a trigger with tempo meaning)
};

/** Where a node runs. VOICE: once per sounding note (its lane's voice pool); GLOBAL: once;
    EVENT: note-stream processing before audio, once per host block. UTILITY types are
    flexible (their instance picks VOICE or GLOBAL). */
enum class Domain : std::uint8_t { voice, global, event };

enum class Polarity : std::uint8_t { unipolar, bipolar };

/** How a parameter maps 0..1 (normalised) to its native range, and how scenes interpolate it. */
enum class ParamCurve : std::uint8_t
{
    linear,
    logarithmic,   ///< Hz, seconds: geometric between min and max (min > 0)
    integer,
    choice,        ///< enum index; scenes switch it at the midpoint
    toggle         ///< bool; scenes switch it at the midpoint
};

/** Domain masks for descriptors (bit per Domain). */
constexpr std::uint8_t domainBit (Domain d) noexcept { return static_cast<std::uint8_t> (1u << static_cast<unsigned> (d)); }
constexpr std::uint8_t voiceOnly = domainBit (Domain::voice);
constexpr std::uint8_t globalOnly = domainBit (Domain::global);
constexpr std::uint8_t eventOnly = domainBit (Domain::event);
constexpr std::uint8_t voiceOrGlobal = domainBit (Domain::voice) | domainBit (Domain::global);

inline const char* portKindName (PortKind k) noexcept
{
    switch (k)
    {
        case PortKind::notes: return "notes";
        case PortKind::pitch: return "pitch";
        case PortKind::gate: return "gate";
        case PortKind::trigger: return "trigger";
        case PortKind::control: return "control";
        case PortKind::audioMono: return "audio";
        case PortKind::audioStereo: return "stereo";
        case PortKind::clock: return "clock";
    }
    return "?";
}

inline const char* domainName (Domain d) noexcept
{
    switch (d)
    {
        case Domain::voice: return "voice";
        case Domain::global: return "global";
        case Domain::event: return "event";
    }
    return "?";
}

inline bool domainFromName (const std::string& s, Domain& d) noexcept
{
    if (s == "voice") { d = Domain::voice; return true; }
    if (s == "global") { d = Domain::global; return true; }
    if (s == "event") { d = Domain::event; return true; }
    return false;
}

inline bool isAudio (PortKind k) noexcept { return k == PortKind::audioMono || k == PortKind::audioStereo; }
inline bool isControlLike (PortKind k) noexcept
{
    return k == PortKind::control || k == PortKind::pitch || k == PortKind::gate || k == PortKind::trigger || k == PortKind::clock;
}
inline int channelsOf (PortKind k) noexcept { return k == PortKind::audioStereo ? 2 : (k == PortKind::notes ? 0 : 1); }

} // namespace andor::modular
