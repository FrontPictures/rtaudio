#include "RtApiWasapiSystemCallback.h"

RtApiWasapiSystemCallback::RtApiWasapiSystemCallback(RtAudioDeviceCallbackLambda callback) : mNotificationHandler(callback)
{
    HRESULT hr = deviceEnumerator_->RegisterEndpointNotificationCallback(&mNotificationHandler);
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapiSystemCallback::RtApiWasapiSystemCallback: Failed to register endpoint notification");
    }
}

RtApiWasapiSystemCallback::~RtApiWasapiSystemCallback()
{
    HRESULT hr = deviceEnumerator_->UnregisterEndpointNotificationCallback(&mNotificationHandler);
    if (FAILED(hr)) {
        error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapiSystemCallback::RtApiWasapiSystemCallback: Failed to unregister endpoint notification");
    }
}
