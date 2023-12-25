#include "RtApiPulseProber.h"
#include "PaContextWithMainloop.h"
#include "PulseCommon.h"
#include <pulse/pulseaudio.h>

std::optional<RtAudio::DeviceInfo> RtApiPulseProber::probeDevice(const std::string &busId)
{
    auto contextWithLoop = PaContextWithMainloop::Create(nullptr);
    if (!contextWithLoop) {
        errorStream_ << "RtApiPulse::probeDevices: failed to create context with mainloop.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    auto devices_opt = getServerDevices(contextWithLoop->getContext());
    if (!devices_opt) {
        errorStream_ << "PaMainloopRunning::run: get devices failed: ";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    for (auto &d : devices_opt->devices) {
        if (d.partial.busID == busId) {
            return d;
        }
    }
    return {};
}
