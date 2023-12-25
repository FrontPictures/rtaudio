#pragma once
#include "RtAudio.h"

struct pa_mainloop;
struct pa_context;

class RtApiPulseProber : public RtApiProber
{
public:
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    std::optional<RtAudio::DeviceInfo> probeDevice(const std::string &busId) override;
};
