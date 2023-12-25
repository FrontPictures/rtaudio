#include "PulseCommon.h"
#include "pulse/PaContext.h"
#include "pulse/PaMainloop.h"
#include <cassert>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>

namespace {
void rt_pa_set_server_info_cb2(pa_context *, const pa_server_info *info, void *userdata)
{
    assert(userdata);
    auto *serverInfo = reinterpret_cast<ServerInfoStruct *>(userdata);
    if (!info) {
        serverInfo->setError();
        return;
    }
    serverInfo->defaultRate = info->sample_spec.rate;
    serverInfo->defaultSinkName = info->default_sink_name;
    serverInfo->defaultSourceName = info->default_source_name;
    serverInfo->setReady();
}

RtAudio::DeviceInfo rtPaSetInfo(const ServerInfoStruct &serverInfo,
                                int channels,
                                bool output,
                                std::string name,
                                std::string busId)
{
    RtAudio::DeviceInfo info{};
    info.partial.busID = std::move(busId);
    info.partial.name = std::move(name);

    if (!output) {
        info.partial.supportsInput = true;
        info.inputChannels = channels;
    } else {
        info.partial.supportsOutput = true;
        info.outputChannels = channels;
    }
    info.currentSampleRate = serverInfo.defaultRate;
    info.preferredSampleRate = serverInfo.defaultRate;
    for (const unsigned int sr : PULSE_SUPPORTED_SAMPLERATES)
        info.sampleRates.push_back(sr);
    for (const rtaudio_pa_format_mapping_t &fm : pulse_supported_sampleformats)
        info.nativeFormats |= fm.rtaudio_format;
    return info;
}

void rt_pa_set_sink_info_cb2(pa_context * /*c*/, const pa_sink_info *i, int eol, void *userdata)
{
    assert(userdata);
    if (eol) {
        return;
    }
    auto *devicesStruct = reinterpret_cast<ServerDevicesStruct *>(userdata);
    if (!i) {
        devicesStruct->setError();
        return;
    }
    auto info = rtPaSetInfo(devicesStruct->serverInfo,
                            i->sample_spec.channels,
                            true,
                            i->description,
                            i->name);
    devicesStruct->devices.push_back(std::move(info));
}

static void rt_pa_set_source_info_cb_and_quit2(pa_context * /*c*/,
                                               const pa_source_info *i,
                                               int eol,
                                               void *userdata)
{
    assert(userdata);
    auto *devicesStruct = reinterpret_cast<ServerDevicesStruct *>(userdata);
    if (eol) {
        devicesStruct->setReady();
        return;
    }
    if (!i) {
        devicesStruct->setError();
        return;
    }
    auto info = rtPaSetInfo(devicesStruct->serverInfo,
                            i->sample_spec.channels,
                            false,
                            i->description,
                            i->name);
    devicesStruct->devices.push_back(std::move(info));
}
} // namespace

std::optional<ServerInfoStruct> getServerInfo(std::shared_ptr<PaContext> context)
{
    if (!context)
        return {};
    auto loop = context->getMainloop();
    if (!loop)
        return {};

    ServerInfoStruct res{};
    pa_operation *o = pa_context_get_server_info(context->handle(), rt_pa_set_server_info_cb2, &res);
    if (!o) {
        return {};
    }
    loop->runUntil([&]() { return res.isReadyOrError() || context->hasError(); });
    pa_operation_unref(o);
    if (res.isReady() == false) {
        return {};
    }
    return res;
}

std::optional<ServerDevicesStruct> getServerDevices(std::shared_ptr<PaContext> context)
{
    if (!context)
        return {};
    auto loop = context->getMainloop();
    if (!loop)
        return {};
    auto serverInfo_opt = getServerInfo(context);
    if (!serverInfo_opt)
        return {};

    ServerDevicesStruct res;
    res.serverInfo = serverInfo_opt.value();
    pa_operation *operSinks = pa_context_get_sink_info_list(context->handle(),
                                                            rt_pa_set_sink_info_cb2,
                                                            &res);
    pa_operation *operSources = pa_context_get_source_info_list(context->handle(),
                                                                rt_pa_set_source_info_cb_and_quit2,
                                                                &res);

    if (operSinks && operSources) {
        loop->runUntil([&]() { return res.isReadyOrError() || context->hasError(); });
    }

    if (operSinks) {
        pa_operation_unref(operSinks);
    }
    if (operSources) {
        pa_operation_unref(operSources);
    }
    if (res.isReady() == false) {
        return {};
    }
    return res;
}
