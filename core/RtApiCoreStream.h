#pragma once
#include "RtAudio.h"
#include <CoreAudio/AudioHardware.h>

class RtApiCoreStream : public RtApiStreamClass
{
public:
    RtApiCoreStream(RtApi::RtApiStream stream, AudioDeviceID id);
    ~RtApiCoreStream();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::MACOSX_CORE; }
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;

    bool isValid() const;
    // This function is intended for internal use only.  It must be
    // public because it is called by an internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    bool callbackEvent(AudioDeviceID deviceId,
                       const AudioBufferList *inBufferList,
                       const AudioBufferList *outBufferList);
    void signalError();
    void signalXrun(RtApi::StreamMode mode);

private:
    std::mutex mMutex;
    bool mWasErrorInAudio = false;
    RtAudioErrorType stopStreamPriv();
    AudioDeviceID mDeviceId = 0;
    AudioDeviceIOProcID mProcId = nullptr;
    bool mIsValid = false;

    std::atomic_flag mXrunInput = ATOMIC_FLAG_INIT;
    std::atomic_flag mXrunOutput = ATOMIC_FLAG_INIT;
};
