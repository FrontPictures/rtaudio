#pragma once

#include "RtAudio.h"

class RtApiCoreStreamFactory : public RtApiStreamClassFactory
{
public:
    RtApiCoreStreamFactory() = default;
    ~RtApiCoreStreamFactory() = default;
    RtAudio::Api getCurrentApi(void) override { return RtAudio::MACOSX_CORE; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;
};
