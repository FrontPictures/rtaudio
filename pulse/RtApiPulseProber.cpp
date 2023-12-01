#include "RtApiPulseProber.h"
#include "PulseCommon.h"
#include <pulse/pulseaudio.h>

namespace {
static const unsigned int SUPPORTED_SAMPLERATES[]
    = {8000, 16000, 22050, 32000, 44100, 48000, 96000, 192000, 0};

struct rtaudio_pa_format_mapping_t
{
    RtAudioFormat rtaudio_format;
    pa_sample_format_t pa_format;
};

static const rtaudio_pa_format_mapping_t supported_sampleformats[]
    = {{RTAUDIO_SINT16, PA_SAMPLE_S16LE},
       {RTAUDIO_SINT24, PA_SAMPLE_S24LE},
       {RTAUDIO_SINT32, PA_SAMPLE_S32LE},
       {RTAUDIO_FLOAT32, PA_SAMPLE_FLOAT32LE},
       {0, PA_SAMPLE_INVALID}};

struct PaDeviceProbeInfo
{
    pa_mainloop_api *paMainLoopApi = nullptr;
    int defaultRate = 0;
    std::optional<RtAudio::DeviceInfo> deviceInfo;
    std::string busId;
};

void rt_pa_set_server_info(pa_context *, const pa_server_info *info, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (!info) {
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 1);
        return;
    }
    paProbeInfo->defaultRate = info->sample_spec.rate;
}

std::optional<RtAudio::DeviceInfo> rt_pa_set_info(PaDeviceProbeInfo *paProbeInfo,
                                                  int channels,
                                                  std::string name,
                                                  std::string busId,
                                                  RtApi::StreamMode mode)
{
    if (paProbeInfo->busId != busId)
        return {};

    RtAudio::DeviceInfo info{};
    info.partial.busID = paProbeInfo->busId;
    info.partial.name = std::move(name);

    if (mode == RtApi::INPUT) {
        info.partial.supportsInput = true;
        info.inputChannels = channels;
    } else if (mode == RtApi::OUTPUT) {
        info.partial.supportsOutput = true;
        info.outputChannels = channels;
    }
    info.currentSampleRate = paProbeInfo->defaultRate;
    info.preferredSampleRate = paProbeInfo->defaultRate;
    for (const unsigned int *sr = SUPPORTED_SAMPLERATES; *sr; ++sr)
        info.sampleRates.push_back(*sr);
    for (const rtaudio_pa_format_mapping_t *fm = supported_sampleformats; fm->rtaudio_format; ++fm)
        info.nativeFormats |= fm->rtaudio_format;
    return info;
}

// Used to get output device information.
void rt_pa_set_source_info(pa_context * /*c*/, const pa_source_info *i, int eol, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (eol) {
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 1);
        return;
    }
    if (!i)
        return;
    int channels = 0;
    std::string name;
    std::string busId = i->name;
    channels = i->sample_spec.channels;
    name = i->description;
    auto info = rt_pa_set_info(paProbeInfo,
                               channels,
                               std::move(name),
                               std::move(busId),
                               RtApi::INPUT);
    if (info) {
        paProbeInfo->deviceInfo = std::move(info);
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 0);
    }
}
void rt_pa_set_sink_info(pa_context * /*c*/, const pa_sink_info *i, int eol, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (eol) {
        return;
    }
    if (!i)
        return;
    int channels = 0;
    std::string name;
    std::string busId;
    channels = i->sample_spec.channels;
    name = i->description;
    busId = i->name;
    auto info = rt_pa_set_info(paProbeInfo,
                               channels,
                               std::move(name),
                               std::move(busId),
                               RtApi::OUTPUT);
    if (info) {
        paProbeInfo->deviceInfo = std::move(info);
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 0);
    }
}

// This is the initial function that is called when the callback is
// set. This one then calls the functions above.
void rt_pa_context_state_callback(pa_context *context, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    auto state = pa_context_get_state(context);
    switch (state) {
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
        break;

    case PA_CONTEXT_READY:
        pa_context_get_server_info(context, rt_pa_set_server_info, userdata);
        pa_context_get_sink_info_list(context, rt_pa_set_sink_info, userdata);
        pa_context_get_source_info_list(context, rt_pa_set_source_info, userdata);
        break;
    case PA_CONTEXT_TERMINATED:
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 0);
        break;

    case PA_CONTEXT_FAILED:
    default:
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 1);
    }
}
} // namespace

std::optional<RtAudio::DeviceInfo> RtApiPulseProber::probeDevice(const std::string &busId)
{
    PaMainloop ml;
    if (ml.isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_new() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    PaContext context(pa_mainloop_get_api(ml.handle()));
    if (context.isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    auto info = probeInfoHandle(ml.handle(), context.handle(), busId);
    return info;
}

std::optional<RtAudio::DeviceInfo> RtApiPulseProber::probeInfoHandle(pa_mainloop *ml,
                                                                     pa_context *context,
                                                                     const std::string &busId)
{
    if (!ml || !context)
        return {};
    int ret = 1;
    PaDeviceProbeInfo paProbeInfo{};
    paProbeInfo.busId = busId;
    paProbeInfo.paMainLoopApi = pa_mainloop_get_api(ml);
    pa_context_set_state_callback(context, rt_pa_context_state_callback, &paProbeInfo);

    if (pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_connect() failed: "
                     << pa_strerror(pa_context_errno(context));
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    if (pa_mainloop_run(ml, &ret) < 0) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_run() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    if (paProbeInfo.deviceInfo.has_value() == false) {
        errorStream_ << "RtApiPulse::probeDevices: could not get server info.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    return paProbeInfo.deviceInfo;
}
