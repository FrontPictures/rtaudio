#pragma once

#include "RtAudio.h"
#include "alsa/asoundlib.h"
#include <atomic>
#include <condition_variable>

class RtApiAlsaStream : public RtApiStreamClass
{
public:
    RtApiAlsaStream(RtApi::RtApiStream stream,
                    snd_pcm_t *phandlePlayback,
                    snd_pcm_t *phandleCapture);
    ~RtApiAlsaStream();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_ALSA; }
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    void callbackEvent(void);

private:
    bool processAudio();
    bool processInput();
    bool processOutput();
    void updateStreamLatency(snd_pcm_t *handle, RtApi::StreamMode mode);

    bool setupThread();
    std::atomic_bool mStopFlag = false;
    std::atomic_bool mRunningFlag = false;

    std::mutex mThreadMutex;
    std::condition_variable mThreadCV;
    std::condition_variable mThreadPausedCV;

    snd_pcm_t *mHandlePlayback = nullptr;
    snd_pcm_t *mHandleCapture = nullptr;

    bool mXrunOutput = false;
    bool mXrunInput = false;
};
