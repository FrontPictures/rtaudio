#pragma once

#include "RtAudio.h"
#include "WasapiNotificationHandler.h"
#include "WasapiCommon.h"

class RTAUDIO_DLL_PUBLIC RtApiWasapiSystemCallback : public RtApiSystemCallback, public RtApiWasapiCommon {
public:
    RtApiWasapiSystemCallback(RtAudioDeviceCallbackLambda callback);
    ~RtApiWasapiSystemCallback();

    RtAudio::Api getCurrentApi(void) { return RtAudio::WINDOWS_WASAPI; }
private:
    NotificationHandler mNotificationHandler;
};
