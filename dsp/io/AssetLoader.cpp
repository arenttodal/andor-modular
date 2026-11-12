#include "io/AssetLoader.h"

#include "graph/Document.h"
#include "io/Wav.h"

#include "analysis/Analyzer.h"
#include "engine/InstrumentBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace andor::modular
{

AssetLibrary::AssetLibrary() : decoder ([] (const std::string& path, osp::AudioData& out, std::string& error) { return wav::read (path, out, error); })
{
    thread = std::thread ([this] { worker(); });
}

AssetLibrary::~AssetLibrary()
{
    {
        std::lock_guard<std::mutex> l (lock);
        quit = true;
    }
    wake.notify_all();
    if (thread.joinable())
        thread.join();
}

void AssetLibrary::setDecoder (Decoder d)
{
    std::lock_guard<std::mutex> l (lock);
    decoder = std::move (d);
}

void AssetLibrary::request (const std::string& key)
{
    if (key.empty())
        return;
    {
        std::lock_guard<std::mutex> l (lock);
        const auto it = assets.find (key);
        if (it != assets.end() && it->second->status != SampleAsset::Status::failed)
            return;
        auto a = std::make_shared<SampleAsset>();
        a->status = SampleAsset::Status::loading;
        a->path = key;
        a->name = key.substr (key.find_last_of ("/\\") + 1);
        assets[key] = std::move (a);
        queue.push_back (key);
        busy.fetch_add (1);
    }
    gen.fetch_add (1, std::memory_order_acq_rel);
    wake.notify_all();
}

std::shared_ptr<const SampleAsset> AssetLibrary::loadNow (const std::string& key, bool withContinuation)
{
    load (key, withContinuation);
    return resolve (key);
}

std::shared_ptr<const SampleAsset> AssetLibrary::resolve (const std::string& key) const
{
    std::lock_guard<std::mutex> l (lock);
    const auto it = assets.find (key);
    return it != assets.end() ? it->second : nullptr;
}

AssetResolver AssetLibrary::resolver()
{
    return [this] (const std::string& key) {
        auto a = resolve (key);
        if (a == nullptr)
        {
            request (key);
            a = resolve (key);
        }
        return a;
    };
}

bool AssetLibrary::waitIdle (double seconds) const
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double> (seconds);
    while (std::chrono::steady_clock::now() < until)
    {
        if (busy.load() == 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    return busy.load() == 0;
}

void AssetLibrary::store (const std::string& key, std::shared_ptr<const SampleAsset> asset)
{
    {
        std::lock_guard<std::mutex> l (lock);
        assets[key] = std::move (asset);
    }
    gen.fetch_add (1, std::memory_order_acq_rel);
}

void AssetLibrary::worker()
{
    while (true)
    {
        std::string key;
        {
            std::unique_lock<std::mutex> l (lock);
            wake.wait (l, [this] { return quit || ! queue.empty(); });
            if (quit)
                return;
            key = queue.front();
            queue.pop_front();
        }
        load (key, true);
        busy.fetch_sub (1);
    }
}

void AssetLibrary::load (const std::string& key, bool withContinuation)
{
    auto base = std::make_shared<SampleAsset>();
    base->path = key;
    base->name = key.substr (key.find_last_of ("/\\") + 1);

    osp::AudioData audio;
    std::string error;
    Decoder decode;
    {
        std::lock_guard<std::mutex> l (lock);
        decode = decoder;
    }
    bool ok = false;
    try
    {
        ok = decode (key, audio, error);
    }
    catch (...)
    {
        error = "the decoder failed";
    }
    if (! ok)
    {
        // A file that is not there is MISSING (relink keeps the patch); anything else FAILED.
        base->status = error.rfind ("cannot open", 0) == 0 ? SampleAsset::Status::missing : SampleAsset::Status::failed;
        base->message = error;
        store (key, base);
        return;
    }
    if (audio.numFrames() < 64 || audio.sampleRate <= 0.0)
    {
        base->status = SampleAsset::Status::failed;
        base->message = "the recording is too short to play";
        store (key, base);
        return;
    }
    // Hash the audio (content identity; the dependency manifest and relink by content).
    {
        std::string bytes;
        for (const auto& ch : audio.channels)
            bytes.append (reinterpret_cast<const char*> (ch.data()), std::min<std::size_t> (ch.size(), 48000 * 4) * sizeof (float));
        base->hash = contentHash (bytes);
    }
    base->durationSeconds = audio.durationSeconds();
    // Overview for drawing: 128 peaks.
    base->overview.assign (128, 0.0f);
    const auto frames = audio.numFrames();
    for (std::int64_t f = 0; f < frames; ++f)
    {
        float v = 0.0f;
        for (const auto& ch : audio.channels)
            v = std::max (v, std::abs (ch[static_cast<std::size_t> (f)]));
        auto& slot = base->overview[static_cast<std::size_t> (f * 128 / std::max<std::int64_t> (1, frames))];
        slot = std::max (slot, v);
    }
    analyseRhythm (audio, base->tempoBpm, base->beats, base->onsetsSeconds);

    try
    {
        osp::AnalysisOptions options;
        options.maxAnalysisSeconds = 60.0;
        const auto analysis = osp::Analyzer::analyse (audio, options);
        osp::InstrumentBuildOptions build;
        auto provisional = osp::instrument::buildProvisional (audio, analysis, build);
        if (provisional == nullptr || ! provisional->isValid())
        {
            base->status = SampleAsset::Status::failed;
            base->message = "the recording could not be prepared";
            store (key, base);
            return;
        }
        auto first = std::make_shared<SampleAsset> (*base);
        first->status = SampleAsset::Status::provisional;
        first->model = provisional;
        first->rootMidi = provisional->rootMidi;
        first->pitchConfidence = analysis.pitch.confidence;
        store (key, first);
        if (withContinuation)
        {
            auto continued = osp::instrument::addContinuation (*provisional, audio, build);
            auto done = std::make_shared<SampleAsset> (*first);
            done->status = SampleAsset::Status::ready;
            if (continued != nullptr && continued->isValid())
                done->model = continued;
            store (key, done);
        }
    }
    catch (...)
    {
        base->status = SampleAsset::Status::failed;
        base->message = "analysis failed";
        store (key, base);
    }
}

void AssetLibrary::analyseRhythm (const osp::AudioData& audio, double& bpm, double& beats, std::vector<double>& onsets)
{
    bpm = 0.0;
    beats = 0.0;
    onsets.clear();
    const double rate = audio.sampleRate;
    const auto frames = audio.numFrames();
    const int hop = std::max (1, static_cast<int> (rate * 0.01));   // 10 ms
    const auto count = static_cast<std::size_t> (frames / hop);
    if (count < 50)
        return;
    // Energy per hop, then its rise (onset strength).
    std::vector<double> energy (count, 0.0), flux (count, 0.0);
    for (std::size_t h = 0; h < count; ++h)
    {
        double e = 0.0;
        for (const auto& ch : audio.channels)
            for (int i = 0; i < hop; ++i)
            {
                const double x = ch[h * static_cast<std::size_t> (hop) + static_cast<std::size_t> (i)];
                e += x * x;
            }
        energy[h] = std::log1p (1000.0 * e / hop);
    }
    for (std::size_t h = 1; h < count; ++h)
        flux[h] = std::max (0.0, energy[h] - energy[h - 1]);
    double mean = 0.0;
    for (const double f : flux)
        mean += f;
    mean /= static_cast<double> (count);
    // Onsets: local peaks of the rise above twice its mean, 60 ms apart at least.
    double lastOnset = -1.0;
    for (std::size_t h = 1; h + 1 < count; ++h)
        if (flux[h] > 2.0 * mean && flux[h] >= flux[h - 1] && flux[h] >= flux[h + 1])
        {
            const double t = static_cast<double> (h) * hop / rate;
            if (lastOnset < 0.0 || t - lastOnset > 0.06)
            {
                onsets.push_back (t);
                lastOnset = t;
            }
        }
    // Tempo: the strongest autocorrelation of the onset strength between 70 and 180 BPM,
    // preferring the one that makes the loop a whole number of beats.
    const double duration = static_cast<double> (frames) / rate;
    double best = -1.0;
    for (double candidate = 70.0; candidate <= 180.0; candidate += 0.5)
    {
        const double lag = 60.0 / candidate / (static_cast<double> (hop) / rate);
        const auto l = static_cast<std::size_t> (std::lround (lag));
        if (l < 2 || l >= count / 2)
            continue;
        double sum = 0.0;
        for (std::size_t h = 0; h + l < count; ++h)
            sum += flux[h] * flux[h + l];
        sum /= static_cast<double> (count - l);
        const double wholeBeats = duration * candidate / 60.0;
        const double fit = 1.0 - std::min (0.5, std::abs (wholeBeats - std::round (wholeBeats)));
        const double score = sum * (0.5 + fit);
        if (score > best)
        {
            best = score;
            bpm = candidate;
        }
    }
    if (bpm > 0.0)
        beats = std::max (1.0, std::round (duration * bpm / 60.0));
}

} // namespace andor::modular
