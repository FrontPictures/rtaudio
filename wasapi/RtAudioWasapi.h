// Authored by Marcus Tomlinson <themarcustomlinson@gmail.com>, April 2014
// Updates for new device selection scheme by Gary Scavone, January 2022
// - Introduces support for the Windows WASAPI API
// - Aims to deliver bit streams to and from hardware at the lowest possible latency, via the absolute minimum buffer sizes required
// - Provides flexible stream configuration to an otherwise strict and inflexible WASAPI interface
// - Includes automatic internal conversion of sample rate and buffer size between hardware and the user

#pragma once

#include "RtAudio.h"
#include <mmdeviceapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfplay.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include "utils.h"
#include "WasapiNotificationHandler.h"

class RtApiWasapi : public RtApi
{
public:
    RtApiWasapi();
    virtual ~RtApiWasapi();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_WASAPI; }    
    void closeStream(void) override;
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;
    RtAudioErrorType abortStream(void) override;
    RtAudioErrorType registerExtraCallback(RtAudioDeviceCallback callback, void* userData) override;
    RtAudioErrorType unregisterExtraCallback() override;
    
private:
    bool coInitialized_;
    IMMDeviceEnumerator* deviceEnumerator_;
    RtAudioDeviceCallback callbackExtra_ = nullptr;
    NotificationHandler wasapiNotificationHandler_;
    std::vector< std::pair< std::string, bool> > deviceIds_;
    
    void listDevices(void) override;
    bool probeSingleDeviceInfo(RtAudio::DeviceInfo& info) override;

    bool probeDeviceInfo(RtAudio::DeviceInfo& info, LPWSTR deviceId, bool isCaptureDevice);
    bool probeDeviceOpen(unsigned int deviceId, StreamMode mode, unsigned int channels,
        unsigned int firstChannel, unsigned int sampleRate,
        RtAudioFormat format, unsigned int* bufferSize,
        RtAudio::StreamOptions* options) override;

    static DWORD WINAPI runWasapiThread(void* wasapiPtr);
    static DWORD WINAPI stopWasapiThread(void* wasapiPtr);
    static DWORD WINAPI abortWasapiThread(void* wasapiPtr);
    void wasapiThread();
};
