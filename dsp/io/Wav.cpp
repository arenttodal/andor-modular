#include "io/Wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace andor::modular::wav
{

namespace
{
    std::uint32_t u32 (const unsigned char* p) noexcept { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t> (p[3]) << 24); }
    std::uint16_t u16 (const unsigned char* p) noexcept { return static_cast<std::uint16_t> (p[0] | (p[1] << 8)); }

    void put32 (std::vector<unsigned char>& v, std::uint32_t x)
    {
        for (int i = 0; i < 4; ++i)
            v.push_back (static_cast<unsigned char> (x >> (8 * i)));
    }
    void put16 (std::vector<unsigned char>& v, std::uint16_t x)
    {
        v.push_back (static_cast<unsigned char> (x));
        v.push_back (static_cast<unsigned char> (x >> 8));
    }
}

bool readMemory (const void* data, std::size_t size, osp::AudioData& out, std::string& error)
{
    const auto* b = static_cast<const unsigned char*> (data);
    if (size < 12 || std::memcmp (b, "RIFF", 4) != 0 || std::memcmp (b + 8, "WAVE", 4) != 0)
    {
        error = "not a WAV file";
        return false;
    }
    int format = 0, channels = 0, bits = 0;
    double rate = 0.0;
    const unsigned char* pcm = nullptr;
    std::size_t pcmBytes = 0;
    std::size_t pos = 12;
    while (pos + 8 <= size)
    {
        const std::uint32_t len = u32 (b + pos + 4);
        const unsigned char* body = b + pos + 8;
        const std::size_t avail = size - (pos + 8);
        if (std::memcmp (b + pos, "fmt ", 4) == 0 && len >= 16 && avail >= 16)
        {
            format = u16 (body);
            channels = u16 (body + 2);
            rate = static_cast<double> (u32 (body + 4));
            bits = u16 (body + 14);
            if (format == 0xFFFE && len >= 26 && avail >= 26)
                format = u16 (body + 24);   // WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
        }
        else if (std::memcmp (b + pos, "data", 4) == 0)
        {
            pcm = body;
            pcmBytes = std::min<std::size_t> (len, avail);
        }
        pos += 8 + static_cast<std::size_t> (len) + (len & 1u);
        if (len > size)
            break;
    }
    if (pcm == nullptr || channels <= 0 || channels > 64 || rate < 1000.0 || rate > 768000.0)
    {
        error = "unsupported or damaged WAV (no data or format)";
        return false;
    }
    const bool isFloat = format == 3;
    if (! ((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64))))
    {
        error = "unsupported WAV encoding (format " + std::to_string (format) + ", " + std::to_string (bits) + " bit)";
        return false;
    }
    const int bytes = bits / 8;
    const std::size_t frames = pcmBytes / static_cast<std::size_t> (bytes * channels);
    out.sampleRate = rate;
    out.channels.assign (static_cast<std::size_t> (channels), std::vector<float> (frames));
    for (std::size_t f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
        {
            const unsigned char* s = pcm + (f * static_cast<std::size_t> (channels) + static_cast<std::size_t> (c)) * static_cast<std::size_t> (bytes);
            float v = 0.0f;
            if (isFloat && bits == 32)
                std::memcpy (&v, s, 4);
            else if (isFloat)
            {
                double d;
                std::memcpy (&d, s, 8);
                v = static_cast<float> (d);
            }
            else if (bits == 8)
                v = (static_cast<float> (s[0]) - 128.0f) / 128.0f;
            else if (bits == 16)
                v = static_cast<float> (static_cast<std::int16_t> (u16 (s))) / 32768.0f;
            else if (bits == 24)
            {
                std::int32_t x = s[0] | (s[1] << 8) | (s[2] << 16);
                if (x & 0x800000)
                    x |= ~0xFFFFFF;
                v = static_cast<float> (x) / 8388608.0f;
            }
            else
                v = static_cast<float> (static_cast<double> (static_cast<std::int32_t> (u32 (s))) / 2147483648.0);
            out.channels[static_cast<std::size_t> (c)][f] = std::isfinite (v) ? v : 0.0f;
        }
    return true;
}

bool read (const std::string& path, osp::AudioData& out, std::string& error)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
    {
        error = "cannot open " + path;
        return false;
    }
    f.seekg (0, std::ios::end);
    const auto size = static_cast<std::size_t> (f.tellg());
    if (size > 2u * 1024u * 1024u * 1024u)
    {
        error = "file too large (over 2 GB)";
        return false;
    }
    f.seekg (0);
    std::vector<unsigned char> bytes (size);
    f.read (reinterpret_cast<char*> (bytes.data()), static_cast<std::streamsize> (size));
    return readMemory (bytes.data(), bytes.size(), out, error);
}

bool write (const std::string& path, const float* left, const float* right, long long frames, double sampleRate, std::string& error, bool floatFormat)
{
    std::vector<unsigned char> v;
    const int bits = floatFormat ? 32 : 24;
    const auto dataBytes = static_cast<std::uint32_t> (frames * 2 * (bits / 8));
    v.insert (v.end(), { 'R', 'I', 'F', 'F' });
    put32 (v, 36 + dataBytes);
    v.insert (v.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
    put32 (v, 16);
    put16 (v, floatFormat ? 3 : 1);
    put16 (v, 2);
    put32 (v, static_cast<std::uint32_t> (sampleRate));
    put32 (v, static_cast<std::uint32_t> (sampleRate) * 2u * static_cast<std::uint32_t> (bits / 8));
    put16 (v, static_cast<std::uint16_t> (2 * bits / 8));
    put16 (v, static_cast<std::uint16_t> (bits));
    v.insert (v.end(), { 'd', 'a', 't', 'a' });
    put32 (v, dataBytes);
    v.reserve (v.size() + dataBytes);
    for (long long i = 0; i < frames; ++i)
        for (const float* ch : { left, right })
        {
            const float x = std::isfinite (ch[i]) ? ch[i] : 0.0f;
            if (floatFormat)
            {
                unsigned char b[4];
                std::memcpy (b, &x, 4);
                v.insert (v.end(), b, b + 4);
            }
            else
            {
                const auto s = static_cast<std::int32_t> (std::lround (std::clamp (x, -1.0f, 1.0f) * 8388607.0f));
                v.push_back (static_cast<unsigned char> (s));
                v.push_back (static_cast<unsigned char> (s >> 8));
                v.push_back (static_cast<unsigned char> (s >> 16));
            }
        }
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f)
    {
        error = "cannot write " + path;
        return false;
    }
    f.write (reinterpret_cast<const char*> (v.data()), static_cast<std::streamsize> (v.size()));
    return f.good();
}

} // namespace andor::modular::wav
