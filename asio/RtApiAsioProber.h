#pragma once

#include "RtAudio.h"

class RtApiAsioProber : public RtApiProber {
public:
    RtApiAsioProber() {}
    ~RtApiAsioProber() {}

    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_ASIO; }
    std::optional<RtAudio::DeviceInfo> probeDevice(const std::string& busId) override;

private:
    std::optional<RtAudio::DeviceInfo> probeDevice2(CLSID clsid);
};
