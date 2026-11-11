// modular-render: renders an ANDOR Modular patch headless (WAV), validates patches, lists the
// module catalogue. Exit codes: 0 ok, 1 usage, 2 unreadable patch, 3 render/write failure.

#include "edit/Factory.h"
#include "graph/Registry.h"
#include "graph/Validator.h"
#include "io/AssetLoader.h"
#include "io/OfflineRender.h"
#include "io/Wav.h"

#include <cctype>
#include <cstdio>
#include <exception>
#include <cstring>
#include <string>

using namespace andor::modular;

namespace
{
    void usage()
    {
        std::puts ("modular-render " MODULAR_VERSION_STRING "\n"
                   "  --patch FILE        .andormodular patch\n"
                   "  --out FILE.wav      render to a 24-bit WAV\n"
                   "  --notes SPEC        note:start:length[:velocity],...  (seconds; default a C major chord)\n"
                   "  --seconds S         length (default 4)\n"
                   "  --rate HZ           sample rate (default 48000)\n"
                   "  --block N           host block size (default 256)\n"
                   "  --bpm BPM           host tempo (default 120)\n"
                   "  --stopped           host transport stopped (free-running clocks)\n"
                   "  --macro I=V         set macro I (1-8) to V (0..1); repeatable\n"
                   "  --scene P           scene position 0..3; --scene-sweep A:B ramps it\n"
                   "  --validate          print the validation report and exit\n"
                   "  --list              print the module catalogue and exit\n"
                   "  --write-factory DIR write the factory patches and modules into DIR");
    }
}

int main (int argc, char** argv)
{
    std::string patch, outPath, notes = "60:0:3:0.8,64:0:3:0.7,67:0:3:0.7";
    RenderSettings s;
    bool validateOnly = false, list = false;
    std::string factoryDir;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&] () -> std::string { return i + 1 < argc ? argv[++i] : std::string {}; };
        if (a == "--patch") patch = next();
        else if (a == "--out") outPath = next();
        else if (a == "--notes") notes = next();
        else if (a == "--seconds") s.seconds = std::atof (next().c_str());
        else if (a == "--rate") s.sampleRate = std::atof (next().c_str());
        else if (a == "--block") s.blockSize = std::atoi (next().c_str());
        else if (a == "--bpm") s.bpm = std::atof (next().c_str());
        else if (a == "--stopped") s.hostPlaying = false;
        else if (a == "--scene") s.scenePosition = std::atof (next().c_str());
        else if (a == "--scene-sweep")
        {
            const auto v = next();
            const auto colon = v.find (':');
            s.sceneFrom = std::atof (v.substr (0, colon).c_str());
            s.sceneTo = colon == std::string::npos ? s.sceneFrom : std::atof (v.substr (colon + 1).c_str());
        }
        else if (a == "--macro")
        {
            const auto v = next();
            const auto eq = v.find ('=');
            const int index = std::atoi (v.substr (0, eq).c_str()) - 1;
            if (eq != std::string::npos && index >= 0 && index < maxMacros)
            {
                s.macros[static_cast<std::size_t> (index)] = static_cast<float> (std::atof (v.substr (eq + 1).c_str()));
                s.setMacros = true;
            }
        }
        else if (a == "--validate") validateOnly = true;
        else if (a == "--list") list = true;
        else if (a == "--write-factory") factoryDir = next();
        else
        {
            usage();
            return 1;
        }
    }
    const auto& reg = Registry::builtIn();
    if (list)
    {
        for (const auto* d : reg.all())
            std::printf ("%-20s %-22s %-12s %s%s\n", d->type.c_str(), d->name.c_str(), d->category.c_str(), d->description.c_str(),
                         d->available ? "" : "  [unavailable]");
        return 0;
    }
    if (! factoryDir.empty())
    {
        try
        {
            for (const auto& p : factory::patches (reg))
            {
                std::string file = p.name, error;
                for (auto& ch : file)
                    ch = ch == ' ' ? '-' : static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
                if (! saveToFile (p, factoryDir + "/" + file + ".andormodular", error))
                {
                    std::fprintf (stderr, "error: %s\n", error.c_str());
                    return 3;
                }
                std::printf ("wrote %s\n", (file + ".andormodular").c_str());
            }
            for (const auto& m : factory::modules (reg))
            {
                std::string file = m.name;
                for (auto& ch : file)
                    ch = ch == ' ' ? '-' : static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
                const auto path = factoryDir + "/modules/" + file + ".andormodule";
                FILE* f = std::fopen (path.c_str(), "wb");
                if (f == nullptr)
                {
                    std::fprintf (stderr, "error: cannot write %s\n", path.c_str());
                    return 3;
                }
                std::fwrite (m.text.data(), 1, m.text.size(), f);
                std::fclose (f);
                std::printf ("wrote modules/%s.andormodule\n", file.c_str());
            }
        }
        catch (const std::exception& e)
        {
            std::fprintf (stderr, "error: %s\n", e.what());
            return 3;
        }
        return 0;
    }
    if (patch.empty() || (outPath.empty() && ! validateOnly))
    {
        usage();
        return 1;
    }
    Document doc;
    const auto loaded = loadFromFile (patch, doc);
    if (! loaded.ok)
    {
        std::fprintf (stderr, "error: %s\n", loaded.error.c_str());
        return 2;
    }
    for (const auto& w : loaded.warnings)
        std::fprintf (stderr, "warning: %s\n", w.c_str());
    Validator v (reg);
    const auto report = v.validate (doc);
    if (validateOnly)
    {
        std::printf ("%s: %s\n", doc.name.c_str(), report.summary().c_str());
        return report.hasErrors() ? 2 : 0;
    }
    // Recordings: keys relative to the patch's folder resolve there.
    AssetLibrary assets;
    const auto folder = patch.substr (0, patch.find_last_of ("/\\") + 1);
    for (const auto& key : doc.assetKeys())
    {
        auto a = assets.loadNow (key[0] == '/' ? key : folder + key);
        std::fprintf (stderr, "asset %s: %s %s\n", key.c_str(), a != nullptr && a->model != nullptr ? "ready" : "unavailable",
                      a != nullptr ? a->message.c_str() : "");
    }
    s.assets = [&] (const std::string& key) { return assets.resolve (key[0] == '/' ? key : folder + key); };
    s.notes = parseScore (notes);
    OfflineRenderer renderer;
    const auto r = renderer.render (doc, s);
    std::string error;
    if (! wav::write (outPath, r.left.data(), r.right.data(), static_cast<long long> (r.left.size()), s.sampleRate, error))
    {
        std::fprintf (stderr, "error: %s\n", error.c_str());
        return 3;
    }
    std::printf ("%s -> %s: %.2f s, peak %.3f, rms %.4f, voices %d, latency %d, %.1fx realtime, compile %.2f ms\n", doc.name.c_str(), outPath.c_str(),
                 s.seconds, r.peak, r.rms, r.maxVoices, r.graph != nullptr ? r.graph->latencySamples : 0, r.realtimeFactor,
                 r.graph != nullptr ? r.graph->compileMilliseconds : 0.0);
    return 0;
}
