// Code written by Peter Meerwald, pmeerw@pmeerw.net and Tristan Matthews.
// Updated by Gary Scavone, 2021.
#pragma once
#include "RtAudio.h"
#include <pulse/pulseaudio.h>

class RtApiPulseEnumerator : public RtApiEnumerator
{
public:
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    virtual std::vector<RtAudio::DeviceInfoPartial> listDevices(void) override;
    virtual std::string getDefaultDevice(RtApi::StreamMode mode) override;
};
