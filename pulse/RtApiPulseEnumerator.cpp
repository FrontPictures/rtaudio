#include "RtApiPulseEnumerator.h"
#include "PulseCommon.h"
#include "pulse/PaContext.h"
#include "pulse/PaMainloopRunning.h"

namespace {

struct OpaqueResultError
{
public:
    void setError() { mError = true; }
    void setReady() { mReady = true; }
    bool isReady() const { return mReady; }
    bool isError() const { return mError; }
    bool isReadyOrError() const { return isReady() || isError(); }

private:
    bool mReady = false;
    bool mError = false;
};

struct ServerInfoStruct : public OpaqueResultError
{
    unsigned int defaultRate = 0;
    std::string defaultSinkName;
    std::string defaultSourceName;
};

struct ServerDevicesStruct : public OpaqueResultError
{
    std::vector<RtAudio::DeviceInfoPartial> devices;
};

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
    RtAudio::DeviceInfoPartial info{};
    info.busID = i->name;
    info.name = i->description;
    info.supportsOutput = true;
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
    RtAudio::DeviceInfoPartial info{};
    info.busID = i->name;
    info.name = i->description;
    info.supportsInput = true;
    devicesStruct->devices.push_back(std::move(info));
}

std::optional<ServerDevicesStruct> getServerDevices(std::shared_ptr<PaContext> context)
{
    if (!context)
        return {};
    auto loop = context->getMainloop();
    if (!loop)
        return {};

    ServerDevicesStruct res;
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
} // namespace

std::vector<RtAudio::DeviceInfoPartial> RtApiPulseEnumerator::listDevices()
{
    std::shared_ptr<PaMainloop> ml = std::make_shared<PaMainloop>();

    if (ml->isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_new() failed.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    std::shared_ptr<PaContext> context = std::make_shared<PaContext>(ml);
    if (context->isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    if (context->connect(nullptr) == false) {
        errorStream_ << "PaMainloopRunning::run: pa_context_connect() failed: "
                     << pa_strerror(pa_context_errno(context->handle()));
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    auto devices = getServerDevices(context);
    if (!devices) {
        errorStream_ << "PaMainloopRunning::run: get devices failed: ";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    return devices->devices;
}
