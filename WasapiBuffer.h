#pragma once

#include "RtAudio.h"

// WASAPI dictates stream sample rate, format, channel count, and in some cases, buffer size.
// Therefore we must perform all necessary conversions to user buffers in order to satisfy these
// requirements. WasapiBuffer ring buffers are used between HwIn->UserIn and UserOut->HwOut to
// provide intermediate storage for read / write synchronization.

class WasapiBuffer
{
public:
    WasapiBuffer();
    ~WasapiBuffer();

    // sets the length of the internal ring buffer
    void setBufferSize(unsigned int bufferSize, unsigned int formatBytes);

    // attempt to push a buffer into the ring buffer at the current "in" index
    bool pushBuffer(char* buffer, unsigned int bufferSize, RtAudioFormat format);

    // attempt to pull a buffer from the ring buffer from the current "out" index
    bool pullBuffer(char* buffer, unsigned int bufferSize, RtAudioFormat format);

private:
    char* buffer_;
    unsigned int bufferSize_;
    unsigned int inIndex_;
    unsigned int outIndex_;
};
