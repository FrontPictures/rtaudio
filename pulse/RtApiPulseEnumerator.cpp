#include "RtApiPulseEnumerator.h"
#include "PulseCommon.h"
#include "pulse/PaMainloopRunning.h"

namespace {
struct PaDeviceProbeInfo : public PaMainloopRunningUserdata
{
    std::string defaultSinkName;
    std::string defaultSourceName;
    int defaultRate = 0;
    std::vector<RtAudio::DeviceInfoPartial> devices;
};

// The following 3 functions are called by the device probing
// system. This first one gets overall system information.
void rt_pa_set_server_info_cb(pa_context *, const pa_server_info *info, void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (!info) {
        paProbeInfo->finished(1);
        return;
    }
    paProbeInfo->defaultRate = info->sample_spec.rate;
    paProbeInfo->defaultSinkName = info->default_sink_name;
    paProbeInfo->defaultSourceName = info->default_source_name;
}

// Used to get output device information.
void rt_pa_set_sink_info_cb(pa_context * /*c*/, const pa_sink_info *i, int eol, void *userdata)
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
static void rt_pa_set_source_info_cb_and_quit(pa_context * /*c*/,
                                              const pa_source_info *i,
                                              int eol,
                                              void *userdata)
{
    PaDeviceProbeInfo *paProbeInfo = static_cast<PaDeviceProbeInfo *>(userdata);
    if (eol) {
        paProbeInfo->finished(0);
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
    PaDeviceProbeInfo paProbeInfo{};
    paProbeInfo.setMainloop(ml);
    if (paProbeInfo.isValid() == false) {
        error(RTAUDIO_WARNING, "RtApiPulseProber::probeInfoHandle: failed create probe info.");
        return {};
    }

    PaMainloopRunning mMainloopRunning(ml, context, rt_pa_context_state_cb, &paProbeInfo);
    auto res = mMainloopRunning.run();

    if (res != RTAUDIO_NO_ERROR) {
        errorStream_ << "RtApiPulse::probeDevices: could not get server info.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    return paProbeInfo.devices;
}
