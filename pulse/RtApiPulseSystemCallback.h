#pragma once

#include "RtAudio.h"
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

    struct PaEventsUserData
    {
        pa_mainloop_api *paMainLoopApi = nullptr;
        RtAudioDeviceCallbackLambda callback = nullptr;
    };

private:
    PaEventsUserData mUserData;

    void notificationThread();
    std::thread mNotificationThread;
    bool initWithHandles(pa_mainloop *ml, pa_context *context);
    RtAudioDeviceCallbackLambda mCallback;
};
