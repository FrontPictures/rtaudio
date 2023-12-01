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
    for (auto &f : pulse_supported_sampleformats) {
        if (f.rtaudio_format == rtf) {
            return f.pa_format;
        }
    }
    return PA_SAMPLE_INVALID;
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
    PaMainloop(PaMainloop &&other) { swap(*this, other); }
    PaMainloop &operator=(PaMainloop other) noexcept
    {
        swap(*this, other);
        return *this;
    }
    void swap(PaMainloop &first, PaMainloop &second) noexcept
    {
        using std::swap;
        swap(first.mMainloop, second.mMainloop);
    }

private:
    pa_mainloop *mMainloop = NULL;
};

class PaContext
{
public:
    PaContext(pa_mainloop_api *api);
    ~PaContext();
    bool isValid() const;
    pa_context *handle() const;

    PaContext(const PaContext &) = delete;
    PaContext &operator=(const PaContext &) = delete;
    PaContext(PaContext &&other) { swap(*this, other); }
    PaContext &operator=(PaContext other) noexcept
    {
        swap(*this, other);
        return *this;
    }
    void swap(PaContext &first, PaContext &second) noexcept
    {
        using std::swap;
        swap(first.mContext, second.mContext);
    }

private:
    pa_context *mContext = NULL;
};
