#pragma once

#include "RtAudio.h"
#include <optional>
#include "WasapiCommon.h"

class RtApiWasapiEnumerator : public RtApiEnumerator, public RtApiWasapiCommon {
public:
    RtApiWasapiEnumerator() {}
    ~RtApiWasapiEnumerator() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }
    virtual std::vector<RtAudio::DeviceInfoPartial> listDevices(void) override;
    virtual std::string getDefaultDevice(RtApi::StreamMode mode) override;
};
