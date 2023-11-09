#include "RtAudio.h"
#include "WasapiCommon.h"
#include "windowscommon.h"

class RtApiWasapiProber : public RtApiProber, public RtApiWasapiCommon {
public:
    RtApiWasapiProber() {}
    ~RtApiWasapiProber() {}

    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }
    std::optional<RtAudio::DeviceInfo> probeDevice(const std::string& busId) override;

private:
    void probeFormats(const UNIQUE_FORMAT& deviceFormat, RtAudio::DeviceInfo& info);
};
