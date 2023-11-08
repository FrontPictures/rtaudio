#pragma once

#include "AsioCommon.h"
#include "RtAudio.h"

class RtApiAsioStream;

class RtApiAsioStreamFactory : public RtApiStreamClassFactory {
public:
    RtApiAsioStreamFactory() {}
    ~RtApiAsioStreamFactory() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_ASIO; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;
    std::shared_ptr<RtApiAsioStream> createAsioStream(const char* driverName, CreateStreamParams params, RtApi::RtApiStream& stream_);
private:
};
