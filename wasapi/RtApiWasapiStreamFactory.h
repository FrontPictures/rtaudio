#pragma once
#include "RtAudio.h"
#include "WasapiCommon.h"

class RtApiWasapiStreamFactory : public RtApiWasapiCommon, public RtApiStreamClassFactory {
public:
    RtApiWasapiStreamFactory() {}
    ~RtApiWasapiStreamFactory() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;
private:
};
