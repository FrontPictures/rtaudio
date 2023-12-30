#pragma once

#include "RtAudio.h"
#include <algorithm>
#include <array>
#include <pulse/sample.h>

struct pa_mainloop;
struct pa_context;
struct pa_mainloop_api;
class PaContext;

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

struct PulseProfileInfo
{
    std::string name;
    std::string description;
    uint32_t priority = 0;
    bool active = false;
};

struct PulseCardInfo
{
    uint32_t index = 0;
    std::string name;
    std::string driver;
    std::vector<PulseProfileInfo> profiles;
};

struct PulsePortInfo
{
    std::string name;
    std::string desc;
    uint32_t priority = 0;
    bool available = false;
    bool active = false;
};

enum class PulseSinkSourceType { SINK, SOURCE };

struct PulseSinkSourceInfo
{
    std::string name;
    uint32_t index = 0;
    std::string description;
    std::string driver;
    uint32_t card = 0;
    std::vector<PulsePortInfo> ports;
    PulseSinkSourceType type;
};

struct OpaqueResultError
{
public:
    void setReady();
    bool isReady() const { return mReady; }
private:
    bool mReady = false;
};

struct ServerInfoStruct : public OpaqueResultError
{
    unsigned int defaultRate = 0;
    std::string defaultSinkName;
    std::string defaultSourceName;
};

struct ServerDevicesStruct : public OpaqueResultError
{
    std::vector<RtAudio::DeviceInfo> devices;
    ServerInfoStruct serverInfo;
};

std::optional<ServerInfoStruct> getServerInfo(std::shared_ptr<PaContext> context);
std::optional<ServerDevicesStruct> getServerDevices(std::shared_ptr<PaContext> context);
std::string getProfileNameForSink(std::shared_ptr<PaContext> context, std::string busId);
