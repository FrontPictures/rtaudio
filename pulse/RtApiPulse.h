// Code written by Peter Meerwald, pmeerw@pmeerw.net and Tristan Matthews.
// Updated by Gary Scavone, 2021.
#pragma once

#include <pulse/pulseaudio.h>
#include "RtAudio.h"

class RtApiPulse: public RtApi
{
public:
    ~RtApiPulse();
    RtAudio::Api getCurrentApi() override { return RtAudio::LINUX_PULSE; }
    void closeStream( void ) override;
    RtAudioErrorType startStream( void ) override;
    RtAudioErrorType stopStream( void ) override;
    RtAudioErrorType abortStream( void ) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    void callbackEvent( void );

    struct PaDeviceInfo {
        std::string sinkName;
        std::string sourceName;
    };

private:
    std::vector< PaDeviceInfo > paDeviceList_;

    void listDevices(void) override;
    bool probeSingleDeviceInfo(RtAudio::DeviceInfo& info) override;

    bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                          unsigned int firstChannel, unsigned int sampleRate,
                          RtAudioFormat format, unsigned int *bufferSize,
                          RtAudio::StreamOptions *options ) override;
    void setRateAndFormat(StreamMode mode, RtAudioFormat format, unsigned int sampleRate, pa_sample_spec& ss);
    bool setupThread(RtAudio::StreamOptions *options, pthread_t *pah);
};
