#pragma once

#include "core/AudioData.h"

#include <string>

namespace andor::modular::wav
{

/** Reads RIFF/WAVE: PCM 8/16/24/32 bit and IEEE float 32/64, any channel count and rate.
    Pure C++ (the kernel's tools and tests); the plugin decodes AIFF and FLAC with JUCE. */
bool read (const std::string& path, osp::AudioData& out, std::string& error);
bool readMemory (const void* data, std::size_t size, osp::AudioData& out, std::string& error);

/** Writes 24-bit PCM (or 32-bit float) stereo. */
bool write (const std::string& path, const float* left, const float* right, long long frames, double sampleRate, std::string& error,
            bool floatFormat = false);

} // namespace andor::modular::wav
