#pragma once
#include "RtAudio.h"
#include <alsa/asoundlib.h>

class RtApiAlsaStreamFactory : public RtApiStreamClassFactory
{
public:
    RtApiAlsaStreamFactory() = default;
    ~RtApiAlsaStreamFactory() = default;
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_ALSA; }
    std::shared_ptr<RtApiStreamClass> createStream(CreateStreamParams params) override;

    struct streamOpenData
    {
        snd_pcm_t *phandle = nullptr;
        snd_pcm_access_t deviceAccessMode = SND_PCM_ACCESS_MMAP_INTERLEAVED;
        snd_pcm_format_t deviceFormat = SND_PCM_FORMAT_UNKNOWN;
        bool doByteSwap = false;
        unsigned int deviceChannels = 0;
        unsigned int periods = 1;
        unsigned int bufferSize = 0;
        void free();
    };

private:
    std::shared_ptr<RtApiStreamClass> createStream2(
        CreateStreamParams params,
        std::optional<RtApiAlsaStreamFactory::streamOpenData> &streamOpenPlayback,
        std::optional<RtApiAlsaStreamFactory::streamOpenData> &streamOpenCapture);

    std::optional<streamOpenData> createStreamDirection(snd_pcm_stream_t stream,
                                                        CreateStreamParams params,
                                                        int openMode,
                                                        snd_output_t *out);
    std::optional<streamOpenData> createStreamDirectionHandle(snd_pcm_stream_t stream,
                                                              CreateStreamParams params,
                                                              snd_output_t *out,
                                                              snd_pcm_t *phandle);
};
