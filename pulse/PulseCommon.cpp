#include "PulseCommon.h"
#include "pulse/PaContext.h"
#include "pulse/PaMainloop.h"
#include <cassert>
#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/mainloop.h>

namespace {

struct SinkSourceCardInfo : public OpaqueResultError
{
    uint32_t card = PA_INVALID_INDEX;
};

struct SinkSourceCurrentProfile : public OpaqueResultError
{
    std::string profile;
};

void rt_pa_set_server_info_cb2(pa_context *, const pa_server_info *info, void *userdata)
{
    assert(userdata);
    auto *serverInfo = reinterpret_cast<ServerInfoStruct *>(userdata);
    if (!info) {
        serverInfo->setReady();
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
        return;
    }
    if (i->card == PA_INVALID_INDEX) {
        return;
    }

    auto info = rtPaSetInfo(devicesStruct->serverInfo,
                            i->sample_spec.channels,
                            true,
                            i->description,
                            i->name);

    devicesStruct->devices.push_back(std::move(info));
}

void rt_pa_sink_info_card_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto devicesStruct = reinterpret_cast<SinkSourceCardInfo *>(userdata);
    if (eol) {
        devicesStruct->setReady();
        return;
    }
    if (!i) {
        return;
    }
    if (i->card == 0) {
        return;
    }
    devicesStruct->card = i->card;
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
        return;
    }
    auto info = rtPaSetInfo(devicesStruct->serverInfo,
                            i->sample_spec.channels,
                            false,
                            i->description,
                            i->name);
    devicesStruct->devices.push_back(std::move(info));
}

void rt_pa_card_info_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto *devicesStruct = reinterpret_cast<SinkSourceCurrentProfile *>(userdata);
    if (eol) {
        devicesStruct->setReady();
        return;
    }
    if (!i) {
        return;
    }
    if (!i->active_profile2) {
        return;
    }
    if (!i->active_profile2->name) {
        return;
    }
    devicesStruct->profile = i->active_profile2->name;
}

uint32_t getSinkCardId(std::shared_ptr<PaContext> context, std::string busId)
{
    SinkSourceCardInfo devicesStruct{};
    pa_operation *oper = pa_context_get_sink_info_by_name(context->handle(),
                                                          busId.c_str(),
                                                          rt_pa_sink_info_card_cb,
                                                          &devicesStruct);
    if (!oper)
        return PA_INVALID_INDEX;
    context->getMainloop()->runUntil(
        [&]() { return devicesStruct.isReady() || context->hasError(); });
    return devicesStruct.card;
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
    loop->runUntil([&]() { return res.isReady() || context->hasError(); });
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
        loop->runUntil([&]() { return res.isReady() || context->hasError(); });
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

std::string getProfileNameForSink(std::shared_ptr<PaContext> context, std::string busId)
{
    auto card = getSinkCardId(context, busId);
    if (card == PA_INVALID_INDEX) {
        return {};
    }
    SinkSourceCurrentProfile profile;
    pa_operation *oper = pa_context_get_card_info_by_index(context->handle(),
                                                           card,
                                                           rt_pa_card_info_cb,
                                                           &profile);
    if (!oper)
        return {};
    context->getMainloop()->runUntil([&]() { return profile.isReady() || context->hasError(); });
    pa_operation_unref(oper);    
    return profile.profile;
}

void OpaqueResultError::setReady()
{
    mReady = true;
}
