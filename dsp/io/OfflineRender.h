#pragma once

#include "graph/Compiler.h"
#include "graph/Engine.h"

#include <string>
#include <vector>

namespace andor::modular
{

/** A note in an offline render (seconds). */
struct ScoreNote
{
    int note = 60;
    double start = 0.0, length = 1.0;
    float velocity = 0.8f;
};

struct RenderSettings
{
    double sampleRate = 48000.0;
    int blockSize = 256;
    double seconds = 4.0;
    double bpm = 120.0;
    bool hostPlaying = true;        ///< a transport running from ppq 0
    std::vector<ScoreNote> notes;
    std::array<float, maxMacros> macros {};
    bool setMacros = false;
    double scenePosition = -1.0;    ///< >= 0: set before rendering
    /** Optional: a ramp of the scene position over the render (from -> to). */
    double sceneFrom = -1.0, sceneTo = -1.0;
    AssetResolver assets;
};

struct RenderResult
{
    std::vector<float> left, right;
    CompiledGraph* graph = nullptr;   ///< valid until the OfflineRenderer is destroyed
    double peak = 0.0, rms = 0.0;
    int maxVoices = 0;
    double realtimeFactor = 0.0;      ///< render speed vs real time
};

/** Renders a patch headless: the same Engine, the same chunking, any block size (tests,
    modular-render, golden renders). Deterministic: the same patch, notes and settings give
    the same samples. */
class OfflineRenderer
{
public:
    RenderResult render (const Document& doc, const RenderSettings& settings);
    Engine& engine() noexcept { return eng; }

private:
    Engine eng;
};

/** "60:0:1:0.8,64:0.5:1" -> notes (note:start:length[:velocity], seconds). */
std::vector<ScoreNote> parseScore (const std::string& text);

} // namespace andor::modular
