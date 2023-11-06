#pragma once

#include "RtAudio.h"
#include <optional>

class RtApiAsioEnumerator : public RtApiEnumerator {
public:
    RtApiAsioEnumerator();
    ~RtApiAsioEnumerator() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_ASIO; }
    std::vector<RtAudio::DeviceInfoPartial> listDevices(void) override;
private:
    std::vector<RtAudio::DeviceInfoPartial> mDevices;
};
