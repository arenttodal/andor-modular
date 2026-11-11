#include "io/OfflineRender.h"

#include "graph/Registry.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace andor::modular
{

RenderResult OfflineRenderer::render (const Document& doc, const RenderSettings& s)
{
    RenderResult out;
    eng.prepare (s.sampleRate, s.blockSize);
    CompileOptions options;
    options.sampleRate = s.sampleRate;
    options.maxBlock = s.blockSize;
    options.assets = s.assets;
    auto graph = compileGraph (doc, Registry::builtIn(), options);
    out.graph = graph.get();
    eng.publish (std::move (graph));
    for (int m = 0; m < maxMacros; ++m)
        eng.setMacro (m, s.setMacros ? s.macros[static_cast<std::size_t> (m)] : static_cast<float> (doc.macros[static_cast<std::size_t> (m)].value));
    if (s.scenePosition >= 0.0)
        eng.setScenePosition (static_cast<float> (s.scenePosition));

    const auto total = static_cast<long long> (s.seconds * s.sampleRate);
    out.left.assign (static_cast<std::size_t> (total), 0.0f);
    out.right.assign (static_cast<std::size_t> (total), 0.0f);
    std::vector<MidiMessage> midi;
    const auto started = std::chrono::steady_clock::now();
    for (long long pos = 0; pos < total; pos += s.blockSize)
    {
        const int n = static_cast<int> (std::min<long long> (s.blockSize, total - pos));
        midi.clear();
        for (const auto& note : s.notes)
        {
            const auto on = static_cast<long long> (note.start * s.sampleRate);
            const auto off = static_cast<long long> ((note.start + note.length) * s.sampleRate);
            if (on >= pos && on < pos + n)
                midi.push_back ({ static_cast<int> (on - pos), MidiMessage::Kind::noteOn, 1, static_cast<std::uint8_t> (note.note), note.velocity, 0 });
            if (off >= pos && off < pos + n)
                midi.push_back ({ static_cast<int> (off - pos), MidiMessage::Kind::noteOff, 1, static_cast<std::uint8_t> (note.note), 0.0f, 0 });
        }
        std::stable_sort (midi.begin(), midi.end(), [] (const MidiMessage& a, const MidiMessage& b) { return a.offset < b.offset; });
        if (s.sceneFrom >= 0.0 && s.sceneTo >= 0.0)
            eng.setScenePosition (static_cast<float> (s.sceneFrom + (s.sceneTo - s.sceneFrom) * static_cast<double> (pos) / static_cast<double> (std::max<long long> (1, total))));
        osp::HostTiming t;
        t.valid = s.hostPlaying;
        t.playing = s.hostPlaying;
        t.bpm = s.bpm;
        t.ppq = static_cast<double> (pos) / s.sampleRate * s.bpm / 60.0;
        eng.process (out.left.data() + pos, out.right.data() + pos, n, midi.data(), static_cast<int> (midi.size()), t);
        out.maxVoices = std::max (out.maxVoices, eng.activeVoices());
        eng.collectGarbage();
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - started).count();
    out.realtimeFactor = elapsed > 0.0 ? s.seconds / elapsed : 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < out.left.size(); ++i)
    {
        out.peak = std::max (out.peak, static_cast<double> (std::max (std::abs (out.left[i]), std::abs (out.right[i]))));
        sum += 0.5 * (static_cast<double> (out.left[i]) * out.left[i] + static_cast<double> (out.right[i]) * out.right[i]);
    }
    out.rms = out.left.empty() ? 0.0 : std::sqrt (sum / static_cast<double> (out.left.size()));
    return out;
}

std::vector<ScoreNote> parseScore (const std::string& text)
{
    std::vector<ScoreNote> notes;
    std::stringstream all (text);
    std::string item;
    while (std::getline (all, item, ','))
    {
        std::stringstream fields (item);
        std::string f;
        std::vector<double> v;
        while (std::getline (fields, f, ':'))
            if (! f.empty())
                v.push_back (std::atof (f.c_str()));
        if (v.empty())
            continue;
        ScoreNote n;
        n.note = std::clamp (static_cast<int> (v[0]), 0, 127);
        n.start = v.size() > 1 ? v[1] : 0.0;
        n.length = v.size() > 2 ? v[2] : 1.0;
        n.velocity = v.size() > 3 ? static_cast<float> (v[3]) : 0.8f;
        notes.push_back (n);
    }
    return notes;
}

} // namespace andor::modular
