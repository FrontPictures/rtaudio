#pragma once

#include "RtAudio.h"
#include <optional>
#include <pulse/def.h>
#include <thread>

struct pa_context;
class PaContextWithMainloop;

class RTAUDIO_DLL_PUBLIC RtApiPulseSystemCallback : public RtApiSystemCallback
{
public:
    RtApiPulseSystemCallback(RtAudioDeviceCallbackLambda callback);
    ~RtApiPulseSystemCallback();

    virtual RtAudio::Api getCurrentApi(void) override { return RtAudio::LINUX_PULSE; }
    void handleEvent(pa_context *c, pa_subscription_event_type_t t, uint32_t idx);

    virtual bool hasError() const override;

private:
    void notificationThread();
    void checkDefaultChanged(pa_context *c, uint32_t idx, unsigned int t);

    std::thread mNotificationThread;
    RtAudioDeviceCallbackLambda mCallback;
    std::shared_ptr<PaContextWithMainloop> mContextWithLoop;
    std::atomic_bool mHasError = true;
};
