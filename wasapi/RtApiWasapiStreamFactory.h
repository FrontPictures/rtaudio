#pragma once
#include "RtAudio.h"
#include "WasapiCommon.h"

class RtApiWasapiStreamFactory : public RtApiWasapiCommon, public RtApiStreamClassFactory {
public:
    RtApiWasapiStreamFactory() {}
    ~RtApiWasapiStreamFactory() {}
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }
    std::shared_ptr<RtApiStreamClass> createStream(RtAudio::DeviceInfo device, RtApi::StreamMode mode, unsigned int channels,
        unsigned int sampleRate, RtAudioFormat format, unsigned int bufferSize, RtAudio::StreamOptions* options) override;
private:
};
