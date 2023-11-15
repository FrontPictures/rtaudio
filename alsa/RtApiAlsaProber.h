#pragma once

#include "RtAudio.h"
#include <alsa/asoundlib.h>

class RtApiAlsaProber : public RtApiProber
{
public:
    RtApiAlsaProber() = default;
    ~RtApiAlsaProber() = default;
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_ALSA; }
    std::optional<RtAudio::DeviceInfo> probeDevice(const std::string& busId) override;

private:
    std::optional<RtAudio::DeviceInfo> probeDeviceHandles(snd_pcm_t *handlePlayback, snd_pcm_t *handleCapture, const char* busId);

    std::optional<unsigned int> probeSingleDeviceGetChannels(snd_pcm_hw_params_t *params, const char* busId);
    std::vector<unsigned int> probeSingleDeviceSamplerates(snd_pcm_t *phandle, snd_pcm_hw_params_t *params);
    RtAudioFormat probeSingleDeviceFormats(snd_pcm_t *phandle, snd_pcm_hw_params_t *params);
};
