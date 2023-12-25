#include "RtApiPulseEnumerator.h"
#include "PaContextWithMainloop.h"
#include "PulseCommon.h"
#include "pulse/PaContext.h"

namespace {
std::vector<RtAudio::DeviceInfoPartial> deviceToPartial(
    const std::vector<RtAudio::DeviceInfo> &devices)
{
    std::vector<RtAudio::DeviceInfoPartial> res;
    for (auto &e : devices) {
        res.push_back(e.partial);
    }
    return res;
}
} // namespace

std::vector<RtAudio::DeviceInfoPartial> RtApiPulseEnumerator::listDevices()
{
    auto contextWithLoop = PaContextWithMainloop::Create(nullptr);
    if (!contextWithLoop) {
        errorStream_ << "RtApiPulse::probeDevices: failed to create context with mainloop.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }

    auto devices = getServerDevices(contextWithLoop->getContext());
    if (!devices) {
        errorStream_ << "PaMainloopRunning::run: get devices failed: ";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    return deviceToPartial(devices->devices);
}

std::string RtApiPulseEnumerator::getDefaultDevice(RtApi::StreamMode mode)
{
    auto contextWithLoop = PaContextWithMainloop::Create(nullptr);
    if (!contextWithLoop) {
        errorStream_ << "RtApiPulse::probeDevices: failed to create context with mainloop.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    auto serverInfo = getServerInfo(contextWithLoop->getContext());
    if (!serverInfo) {
        return {};
    }

    if (mode == RtApi::StreamMode::INPUT) {
        return serverInfo->defaultSourceName;
    } else if (mode == RtApi::StreamMode::OUTPUT) {
        return serverInfo->defaultSinkName;
    }
    return {};
}
