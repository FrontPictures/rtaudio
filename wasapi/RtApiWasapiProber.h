#include "RtAudio.h"
#include "WasapiCommon.h"

class RtApiWasapiProber : public RtApiProber, public RtApiWasapiCommon {
public:
    RtApiWasapiProber() {}
    ~RtApiWasapiProber() {}

    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }
    bool probeDevice(RtAudio::DeviceInfo& info) override;

private:
    void probeFormats(const UNIQUE_FORMAT& deviceFormat, RtAudio::DeviceInfo& info);
};