// The OS X CoreAudio API is designed to use a separate callback
// procedure for each of its audio devices.  A single RtAudio duplex
// stream using two different devices is supported here, though it
// cannot be guaranteed to always behave correctly because we cannot
// synchronize these two callbacks.
//
// A property listener is installed for over/underrun information.
// However, no functionality is currently provided to allow property
// listeners to trigger user handlers because it is unclear what could
// be done if a critical stream parameter (buffer size, sample rate,
// device disconnect) notification arrived.  The listeners entail
// quite a bit of extra code and most likely, a user program wouldn't
// be prepared for the result anyway.  However, we do provide a flag
// to the client callback function to inform of an over/underrun.

// A structure to hold various information related to the CoreAudio API
// implementation.
#pragma once

#include "RtAudio.h"
#include <CoreAudio/AudioHardware.h>

class RtApiCore: public RtApi
{
public:

    RtApiCore();
    ~RtApiCore();
    RtAudio::Api getCurrentApi( void ) override { return RtAudio::MACOSX_CORE; }
    void closeStream( void ) override;
    RtAudioErrorType startStream( void ) override;
    RtAudioErrorType stopStream( void ) override;
    RtAudioErrorType abortStream( void ) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by an internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    bool callbackEvent( AudioDeviceID deviceId,
                       const AudioBufferList *inBufferList,
                       const AudioBufferList *outBufferList );
    void signalError();

private:
    void listDevices() override;
    bool probeSingleDeviceInfo(RtAudio::DeviceInfo& info) override;
    bool probeDeviceInfo( AudioDeviceID id, RtAudio::DeviceInfo &info );
    bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                         unsigned int firstChannel, unsigned int sampleRate,
                         RtAudioFormat format, unsigned int *bufferSize,
                         RtAudio::StreamOptions *options ) override;
    static const char* getErrorCode( OSStatus code );
    std::vector< AudioDeviceID > deviceIds_;
};
