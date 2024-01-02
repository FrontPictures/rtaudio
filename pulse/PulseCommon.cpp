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
    if (info->default_sink_name)
        serverInfo->defaultSinkName = info->default_sink_name;
    if (info->default_source_name)
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

struct RtPaSinkInfoCallbackUserdata : public OpaqueResultError
{
    std::vector<PulseSinkSourceInfo> getInfos() const { return infos; }

    template<class T>
    bool addInfo(const T *i, int eol)
    {
        if (eol) {
            setReady();
            return true;
        }
        if (!i)
            return false;
        PulseSinkSourceInfo inf;
        inf.name = i->name;
        inf.index = i->index;
        inf.description = i->description;
        inf.driver = i->driver;
        inf.card = i->card;
        if (typeid(T) == typeid(pa_sink_info)) {
            inf.type = PulseSinkSourceType::SINK;
        } else if (typeid(T) == typeid(pa_source_info)) {
            inf.type = PulseSinkSourceType::SOURCE;
        } else {
            return false;
        }
        for (int p = 0; p < i->n_ports; p++) {
            auto ip = i->ports[p];
            PulsePortInfo port;
            port.name = ip->name;
            port.desc = ip->description;
            port.priority = ip->priority;
            if (ip->available == PA_PORT_AVAILABLE_YES
                || ip->available == PA_PORT_AVAILABLE_UNKNOWN) {
                port.available = true;
            } else {
                port.available = false;
            }
            if (i->active_port == ip) {
                port.active = true;
            } else {
                port.active = false;
            }
            inf.ports.push_back(port);
        }
        infos.push_back(inf);
        return true;
    }

private:
    std::vector<PulseSinkSourceInfo> infos;
};

void rt_pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto *ud = reinterpret_cast<RtPaSinkInfoCallbackUserdata *>(userdata);
    ud->addInfo(i, eol);
}

void rt_pa_source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto *ud = reinterpret_cast<RtPaSinkInfoCallbackUserdata *>(userdata);
    ud->addInfo(i, eol);
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

OpaqueResultError::~OpaqueResultError() {}
void OpaqueResultError::setReady()
{
    mReady = true;
}

namespace PulseCommon {
std::optional<PulseSinkSourceInfo> getSinkSourceInfo(std::shared_ptr<PaContext> context,
                                                     std::string deviceId,
                                                     PulseSinkSourceType type)
{
    RtPaSinkInfoCallbackUserdata userd;

    pa_operation *oper = nullptr;
    if (type == PulseSinkSourceType::SINK) {
        oper = pa_context_get_sink_info_by_name(context->handle(),
                                                deviceId.c_str(),
                                                rt_pa_sink_info_cb,
                                                &userd);
    } else {
        oper = pa_context_get_source_info_by_name(context->handle(),
                                                  deviceId.c_str(),
                                                  rt_pa_source_info_cb,
                                                  &userd);
    }
    if (!oper)
        return {};
    context->getMainloop()->runUntil([&]() {
        auto state = pa_operation_get_state(oper);
        return userd.isReady() || context->hasError() || state != PA_OPERATION_RUNNING;
    });
    pa_operation_unref(oper);
    auto infos = userd.getInfos();
    if (infos.size() != 1) {
        return {};
    }
    auto info = infos[0];
    return info;
}

bool getSinkSourceInfoAsync(std::shared_ptr<PaContext> context,
                            uint32_t id,
                            PulseSinkSourceType type,
                            std::function<void(std::optional<PulseSinkSourceInfo>)> result)
{
    std::shared_ptr<RtPaSinkInfoCallbackUserdata> userd
        = std::make_shared<RtPaSinkInfoCallbackUserdata>();

    pa_operation *oper = nullptr;
    if (type == PulseSinkSourceType::SINK) {
        oper = pa_context_get_sink_info_by_index(context->handle(),
                                                 id,
                                                 rt_pa_sink_info_cb,
                                                 userd.get());
    } else {
        oper = pa_context_get_source_info_by_index(context->handle(),
                                                   id,
                                                   rt_pa_source_info_cb,
                                                   userd.get());
    }
    if (!oper)
        return {};

    std::shared_ptr<PaMainloopTask> task = std::make_shared<PaMainloopTask>(
        oper, std::move(userd), [result](std::shared_ptr<OpaqueResultError> res) {
            auto *ud = dynamic_cast<RtPaSinkInfoCallbackUserdata *>(res.get());
            assert(ud);
            if (!ud) {
                result({});
            }
            auto infos = ud->getInfos();
            if (infos.size() != 1) {
                result({});
            } else {
                result(infos[0]);
            }
        });
    context->getMainloop()->addTask(std::move(task));
    return true;
}

} // namespace PulseCommon
