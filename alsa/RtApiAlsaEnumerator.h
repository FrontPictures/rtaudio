#pragma once

#include "RtAudio.h"
#include <alsa/asoundlib.h>

class RtApiAlsaEnumerator : public RtApiEnumerator
{
public:
    RtApiAlsaEnumerator() = default;
    ~RtApiAlsaEnumerator() = default;

    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_ALSA; }
    virtual std::vector<RtAudio::DeviceInfoPartial> listDevices(void) override;

private:
    bool probeAudioCard(int card, std::vector<RtAudio::DeviceInfoPartial>& devices);
    bool probeAudioCardHandle(snd_ctl_t *handle, int card, std::vector<RtAudio::DeviceInfoPartial>& devices);
    std::optional<RtAudio::DeviceInfoPartial> probeAudioCardDevice(snd_ctl_t* handle, snd_ctl_card_info_t* ctlinfo, int device, int card);
};
