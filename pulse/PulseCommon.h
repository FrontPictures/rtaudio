#pragma once

#include "RtAudio.h"
#include <algorithm>
#include <array>
#include <pulse/sample.h>

struct pa_mainloop;
struct pa_context;
struct pa_mainloop_api;

static constexpr std::array<unsigned int, 8> PULSE_SUPPORTED_SAMPLERATES
    = {8000, 16000, 22050, 32000, 44100, 48000, 96000, 192000};

struct rtaudio_pa_format_mapping_t
{
    RtAudioFormat rtaudio_format;
    pa_sample_format_t pa_format;
};

static constexpr std::array<rtaudio_pa_format_mapping_t, 4> pulse_supported_sampleformats = {
    {{RTAUDIO_SINT16, PA_SAMPLE_S16LE},
     {RTAUDIO_SINT24, PA_SAMPLE_S24LE},
     {RTAUDIO_SINT32, PA_SAMPLE_S32LE},
     {RTAUDIO_FLOAT32, PA_SAMPLE_FLOAT32LE}}};

constexpr pa_sample_format_t getPulseFormatByRt(RtAudioFormat rtf)
{
    auto it = std::ranges::find(pulse_supported_sampleformats,
                                rtf,
                                &rtaudio_pa_format_mapping_t::rtaudio_format);
    if (it == pulse_supported_sampleformats.end()) {
        return PA_SAMPLE_INVALID;
    }
    return it->pa_format;
}

class PaMainloop
{
public:
    PaMainloop();
    ~PaMainloop();
    bool isValid() const;
    pa_mainloop *handle() const;

    PaMainloop(const PaMainloop &) = delete;
    PaMainloop &operator=(const PaMainloop &) = delete;

    bool runUntil(std::function<bool()>);

private:
    pa_mainloop *mMainloop = NULL;
};

