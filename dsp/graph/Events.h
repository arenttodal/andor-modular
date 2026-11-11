#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace andor::modular
{

/** One note-stream event inside a host block. `id` identifies the note through every chain:
    a note-off ends only the voice its own note-on started (several arps may play the same
    pitch into one source without cutting each other: Merge policy). */
struct NoteEvent
{
    enum class Kind : std::uint8_t { noteOff = 0, noteOn = 1, allNotesOff = 2 };
    int offset = 0;                ///< sample in the host block
    Kind kind = Kind::noteOn;
    std::uint8_t note = 60;        ///< MIDI note 0..127
    std::uint8_t channel = 1;
    float velocity = 0.8f;         ///< 0..1
    std::uint32_t id = 0;
};

/** A fixed-capacity, offset-ordered event list (no allocation: lives in the compiled graph). */
struct EventList
{
    static constexpr int capacity = 1024;
    std::array<NoteEvent, capacity> events {};
    int count = 0;
    int overflowed = 0;   ///< events dropped this block (telemetry; never silently stuck: note-offs are kept first)

    void clear() noexcept
    {
        count = 0;
        overflowed = 0;
    }
    void add (const NoteEvent& e) noexcept
    {
        if (count < capacity)
            events[static_cast<std::size_t> (count++)] = e;
        else if (e.kind != NoteEvent::Kind::noteOn)
        {
            // Full: a note-off replaces the latest note-on, so nothing is left hanging.
            for (int i = count - 1; i >= 0; --i)
                if (events[static_cast<std::size_t> (i)].kind == NoteEvent::Kind::noteOn)
                {
                    events[static_cast<std::size_t> (i)] = e;
                    break;
                }
            ++overflowed;
        }
        else
            ++overflowed;
    }
    /** Stable by offset; at one offset note-offs come before note-ons (retrigger order).
        Insertion sort: in place (std::stable_sort may allocate) and fast on the nearly sorted
        lists the engine makes. */
    void sort() noexcept
    {
        auto before = [] (const NoteEvent& a, const NoteEvent& b) {
            if (a.offset != b.offset)
                return a.offset < b.offset;
            return a.kind != NoteEvent::Kind::noteOn && b.kind == NoteEvent::Kind::noteOn;
        };
        for (int i = 1; i < count; ++i)
        {
            const NoteEvent e = events[static_cast<std::size_t> (i)];
            int j = i - 1;
            while (j >= 0 && before (e, events[static_cast<std::size_t> (j)]))
            {
                events[static_cast<std::size_t> (j + 1)] = events[static_cast<std::size_t> (j)];
                --j;
            }
            events[static_cast<std::size_t> (j + 1)] = e;
        }
    }
    const NoteEvent* begin() const noexcept { return events.data(); }
    const NoteEvent* end() const noexcept { return events.data() + count; }
};

/** Host MIDI as the engine receives it (the plugin converts juce::MidiBuffer). */
struct MidiMessage
{
    enum class Kind : std::uint8_t { noteOn, noteOff, pitchBend, controller, allNotesOff, sustain };
    int offset = 0;
    Kind kind = Kind::noteOn;
    std::uint8_t channel = 1, note = 0;
    float value = 0.0f;   ///< velocity 0..1, bend -1..1, controller 0..1
    int controller = 0;
};

} // namespace andor::modular
