#pragma once

#include "RtAudio.h"

class RtApiCoreEnumerator : public RtApiEnumerator
{
public:
    RtApiCoreEnumerator() = default;
    ~RtApiCoreEnumerator() = default;

    RtAudio::Api getCurrentApi(void) override { return RtAudio::MACOSX_CORE; }
    virtual std::vector<RtAudio::DeviceInfoPartial> listDevices(void) override;

private:
};
