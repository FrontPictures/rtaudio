#pragma once

#include "AsioCommon.h"
#include "RtAudio.h"

class RtApiAsioStreamFactory : public RtApiStreamClassFactory {
public:
    RtApiAsioStreamFactory() {}
    ~RtApiAsioStreamFactory() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_ASIO; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;
private:
};
