#pragma once
#include "RtAudio.h"
#include <CoreAudio/AudioHardware.h>

class RtApiCoreStreamFactory : public RtApiStreamClassFactory
{
public:
    RtApiCoreStreamFactory() = default;
    ~RtApiCoreStreamFactory() = default;
    RtAudio::Api getCurrentApi(void) override { return RtAudio::MACOSX_CORE; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;

private:
    struct ScopeStreamStruct
    {
        unsigned int bufferSize = 0;
        unsigned int latency = 0;
    };
    std::optional<ScopeStreamStruct> setupStreamScope(const CreateStreamParams &params,
                                                      AudioObjectPropertyScope scope,
                                                      AudioDeviceID deviceId);
};
