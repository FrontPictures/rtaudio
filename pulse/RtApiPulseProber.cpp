#include "RtApiPulseProber.h"
#include "PulseCommon.h"
#include "pulse/PaMainloopRunning.h"
#include <pulse/pulseaudio.h>

namespace {

struct PaDeviceProbeInfo : public PaMainloopRunningUserdata
{
    int defaultRate = 0;
    std::optional<RtAudio::DeviceInfo> deviceInfo;
    std::string busId;
};

void rt_pa_set_server_info_cb(pa_context *, const pa_server_info *info, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (!info) {
        paProbeInfo->finished(1);
        return;
    }
    paProbeInfo->defaultRate = info->sample_spec.rate;
}

std::optional<RtAudio::DeviceInfo> rtPaSetInfo(PaDeviceProbeInfo *paProbeInfo,
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
    for (const unsigned int sr : PULSE_SUPPORTED_SAMPLERATES)
        info.sampleRates.push_back(sr);
    for (const rtaudio_pa_format_mapping_t &fm : pulse_supported_sampleformats)
        info.nativeFormats |= fm.rtaudio_format;
    return info;
}

// Used to get output device information.
void rt_pa_set_source_info_cb_and_quit(pa_context * /*c*/,
                                       const pa_source_info *i,
                                       int eol,
                                       void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (eol) {
        paProbeInfo->finished(0);
        return;
    }
    if (!i)
        return;
    int channels = 0;
    std::string name;
    std::string busId = i->name;
    channels = i->sample_spec.channels;
    name = i->description;
    auto info = rtPaSetInfo(paProbeInfo, channels, std::move(name), std::move(busId), RtApi::INPUT);
    if (info) {
        paProbeInfo->deviceInfo = std::move(info);
        paProbeInfo->finished(0);
    }
}
void rt_pa_set_sink_info_cb(pa_context * /*c*/, const pa_sink_info *i, int eol, void *userdata)
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
    auto info = rtPaSetInfo(paProbeInfo, channels, std::move(name), std::move(busId), RtApi::OUTPUT);
    if (info) {
        paProbeInfo->deviceInfo = std::move(info);
        paProbeInfo->finished(0);
    }
}

// This is the initial function that is called when the callback is
// set. This one then calls the functions above.
void rt_pa_context_state_cb(pa_context *context, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    auto state = pa_context_get_state(context);
    switch (state) {
    case PA_CONTEXT_READY:
        pa_context_get_server_info(context, rt_pa_set_server_info_cb, userdata);
        pa_context_get_sink_info_list(context, rt_pa_set_sink_info_cb, userdata);
        pa_context_get_source_info_list(context, rt_pa_set_source_info_cb_and_quit, userdata);
        break;
    default:
        break;
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

    /*PaContext context(pa_mainloop_get_api(ml.handle()));
    if (context.isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    auto info = probeInfoHandle(ml.handle(), context.handle(), busId);
    return info;*/
    return {};
}

std::optional<RtAudio::DeviceInfo> RtApiPulseProber::probeInfoHandle(pa_mainloop *ml,
                                                                     pa_context *context,
                                                                     const std::string &busId)
{
    if (!ml || !context)
        return {};

    PaDeviceProbeInfo paProbeInfo{};
    paProbeInfo.setMainloop(ml);
    if (paProbeInfo.isValid() == false) {
        error(RTAUDIO_WARNING, "RtApiPulseProber::probeInfoHandle: failed create probe info.");
        return {};
    }
    paProbeInfo.busId = busId;

    PaMainloopRunning mMainloopRunning(ml, context, rt_pa_context_state_cb, &paProbeInfo);
    auto res = mMainloopRunning.run();

    if (paProbeInfo.deviceInfo.has_value() == false || res != RTAUDIO_NO_ERROR) {
        error(RTAUDIO_WARNING, "RtApiPulse::probeDevices: could not get server info.");
        return {};
    }
    return paProbeInfo.deviceInfo;
}
