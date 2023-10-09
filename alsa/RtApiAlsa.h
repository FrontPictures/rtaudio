#pragma once
#include "RtAudio.h"
#include <alsa/asoundlib.h>

class RtApiAlsa: public RtApi
{
public:
    RtApiAlsa();
    ~RtApiAlsa();
    RtAudio::Api getCurrentApi() override { return RtAudio::LINUX_ALSA; }
    void closeStream( void ) override;
    RtAudioErrorType startStream( void ) override;
    RtAudioErrorType stopStream( void ) override;
    RtAudioErrorType abortStream( void ) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    void callbackEvent( void );

private:
    std::vector<std::pair<std::string, unsigned int>> deviceIdPairs_;

    void listDevices(void) override;
    bool probeSingleDeviceInfo(RtAudio::DeviceInfo& info) override;

    bool probeDeviceInfo( RtAudio::DeviceInfo &info, std::string name );
    bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                          unsigned int firstChannel, unsigned int sampleRate,
                          RtAudioFormat format, unsigned int *bufferSize,
                          RtAudio::StreamOptions *options ) override;    
};
