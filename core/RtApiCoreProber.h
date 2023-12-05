#include "RtAudio.h"

class RtApiCoreProber : public RtApiProber
{
public:
    RtApiCoreProber() = default;
    ~RtApiCoreProber() = default;

    RtAudio::Api getCurrentApi(void) override { return RtAudio::MACOSX_CORE; }
    std::optional<RtAudio::DeviceInfo> probeDevice(const std::string &busId) override;

private:
};
