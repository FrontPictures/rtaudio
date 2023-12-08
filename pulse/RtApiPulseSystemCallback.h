#pragma once

#include "RtAudio.h"
#include "pulse/PaMainloopRunning.h"
#include <optional>
#include <thread>

struct pa_mainloop;
struct pa_context;
struct pa_mainloop_api;

class RTAUDIO_DLL_PUBLIC RtApiPulseSystemCallback : public RtApiSystemCallback
{
public:
    RtApiPulseSystemCallback(RtAudioDeviceCallbackLambda callback);
    ~RtApiPulseSystemCallback();

    virtual RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }

    struct PaEventsUserData : public PaMainloopRunningUserdata
    {
        RtAudioDeviceCallbackLambda callback = nullptr;
    };

private:
    void notificationThread();
    bool init(pa_mainloop *ml, pa_context *context);

    PaEventsUserData mUserData;
    std::unique_ptr<PaMainloopRunning> mMainloopRunning;
    std::thread mNotificationThread;
    RtAudioDeviceCallbackLambda mCallback;
};
