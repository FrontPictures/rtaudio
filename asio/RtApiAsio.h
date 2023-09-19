// The ASIO API is designed around a callback scheme, so this
// implementation is similar to that used for OS-X CoreAudio and unix
// Jack.  The primary constraint with ASIO is that it only allows
// access to a single driver at a time.  Thus, it is not possible to
// have more than one simultaneous RtAudio stream.
//
// This implementation also requires a number of external ASIO files
// and a few global variables.  The ASIO callback scheme does not
// allow for the passing of user data, so we must create a global
// pointer to our callbackInfo structure.
//
// On unix systems, we make use of a pthread condition variable.
// Since there is no equivalent in Windows, I hacked something based
// on information found in
// http://www.cs.wustl.edu/~schmidt/win32-cv-1.html.

#pragma once
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"
#include "RtAudio.h"
#include "iasiothiscallresolver.h"

// Function declarations (definitions at end of section)
static const char* getAsioErrorString(ASIOError result);
static void sampleRateChanged(ASIOSampleRate sRate);
static long asioMessages(long selector, long value, void* message, double* opt);

struct AsioHandle {
    int drainCounter;       // Tracks callback counts when draining
    bool internalDrain;     // Indicates if stop is initiated from callback or not.
    ASIOBufferInfo* bufferInfos;
    HANDLE condition;

    AsioHandle()
        :drainCounter(0), internalDrain(false), bufferInfos(0), condition(nullptr) {}
};

class RtApiAsio : public RtApi
{
public:
    RtApiAsio();
    ~RtApiAsio();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_ASIO; }
    void closeStream(void) override;
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;
    RtAudioErrorType abortStream(void) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    bool callbackEvent(long bufferIndex);

private:

    bool coInitialized_;
    void probeDevices(void) override;
    void listDevices(void) override;
    bool probeDeviceInfo(RtAudio::DeviceInfo& info);
    bool probeDeviceOpen(unsigned int device, StreamMode mode, unsigned int channels,
        unsigned int firstChannel, unsigned int sampleRate,
        RtAudioFormat format, unsigned int* bufferSize,
        RtAudio::StreamOptions* options) override;
};
