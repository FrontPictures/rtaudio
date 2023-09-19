// JACK is a low-latency audio server, originally written for the
// GNU/Linux operating system and now also ported to OS-X and
// Windows. It can connect a number of different applications to an
// audio device, as well as allowing them to share audio between
// themselves.
//
// When using JACK with RtAudio, "devices" refer to JACK clients that
// have ports connected to the server, while ports correspond to device
// channels.  The JACK server is typically started  in a terminal as
// follows:
//
// .jackd -d alsa -d hw:0
//
// or through an interface program such as qjackctl.  Many of the
// parameters normally set for a stream are fixed by the JACK server
// and can be specified when the JACK server is started.  In
// particular,
//
// .jackd -d alsa -d hw:0 -r 44100 -p 512 -n 4
//
// specifies a sample rate of 44100 Hz, a buffer size of 512 sample
// frames, and number of buffers = 4.  Once the server is running, it
// is not possible to override these values.  If the values are not
// specified in the command-line, the JACK server uses default values.
//
// The JACK server does not have to be running when an instance of
// RtApiJack is created, though the function getDeviceCount() will
// report 0 devices found until JACK has been started.  When no
// devices are available (i.e., the JACK server is not running), a
// stream cannot be opened.
#pragma once

#include "RtAudio.h"
#include <jack/jack.h>

class RtApiJack: public RtApi
{
public:

    RtApiJack();
    ~RtApiJack();
    RtAudio::Api getCurrentApi( void ) override { return RtAudio::UNIX_JACK; }
    void closeStream( void ) override;
    RtAudioErrorType startStream( void ) override;
    RtAudioErrorType stopStream( void ) override;
    RtAudioErrorType abortStream( void ) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    bool callbackEvent( unsigned long nframes );

private:
    void probeDevices( void ) override;
    bool probeDeviceInfo( RtAudio::DeviceInfo &info, jack_client_t *client );
    bool probeDeviceOpen( unsigned int deviceId, StreamMode mode, unsigned int channels,
                          unsigned int firstChannel, unsigned int sampleRate,
                          RtAudioFormat format, unsigned int *bufferSize,
                          RtAudio::StreamOptions *options ) override;

    bool shouldAutoconnect_;
};
