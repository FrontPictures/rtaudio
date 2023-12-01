#include "RtApiPulseEnumerator.h"
#include "PulseCommon.h"

namespace {
struct PaDeviceProbeInfo
{
    pa_mainloop_api *paMainLoopApi = nullptr;
    std::string defaultSinkName;
    std::string defaultSourceName;
    int defaultRate = 0;
    std::vector<RtAudio::DeviceInfoPartial> devices;
};

// The following 3 functions are called by the device probing
// system. This first one gets overall system information.
void rt_pa_set_server_info(pa_context *, const pa_server_info *info, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (!info) {
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 1);
        return;
    }
    paProbeInfo->defaultRate = info->sample_spec.rate;
    paProbeInfo->defaultSinkName = info->default_sink_name;
    paProbeInfo->defaultSourceName = info->default_source_name;
}

// Used to get output device information.
void rt_pa_set_sink_info(pa_context * /*c*/, const pa_sink_info *i, int eol, void *userdata)
{
    if (eol)
        return;
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    RtAudio::DeviceInfoPartial info{};
    info.busID = i->name;
    info.name = i->description;
    info.supportsOutput = true;
    paProbeInfo->devices.push_back(std::move(info));
}

// Used to get input device information.
static void rt_pa_set_source_info_and_quit(pa_context * /*c*/,
                                           const pa_source_info *i,
                                           int eol,
                                           void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (eol) {
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 0);
        return;
    }
    RtAudio::DeviceInfoPartial info{};
    info.busID = i->name;
    info.name = i->description;
    info.supportsInput = true;
    paProbeInfo->devices.push_back(std::move(info));
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
        pa_context_get_sink_info_list(context,
                                      rt_pa_set_sink_info,
                                      userdata); // output info ... needs to be before input
        pa_context_get_source_info_list(context,
                                        rt_pa_set_source_info_and_quit,
                                        userdata); // input info
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

std::vector<RtAudio::DeviceInfoPartial> RtApiPulseEnumerator::listDevices()
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

    auto devices = listDevicesHandles(ml.handle(), context.handle());
    return devices;
}

std::vector<RtAudio::DeviceInfoPartial> RtApiPulseEnumerator::listDevicesHandles(pa_mainloop *ml,
                                                                                 pa_context *context)
{
    if (!ml || !context)
        return {};
    int ret = 1;
    PaDeviceProbeInfo paProbeInfo{};
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

    if (ret != 0) {
        errorStream_ << "RtApiPulse::probeDevices: could not get server info.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    return paProbeInfo.devices;
}
