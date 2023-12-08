#pragma once
#include "RtAudio.h"

struct pa_mainloop;
struct pa_context;

class RtApiPulseProber : public RtApiProber
{
public:
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    std::optional<RtAudio::DeviceInfo> probeDevice(const std::string &busId) override;

private:
    std::optional<RtAudio::DeviceInfo> probeInfoHandle(pa_mainloop *ml,
                                                       pa_context *context,
                                                       const std::string &busId);
};
