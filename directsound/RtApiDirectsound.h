#pragma once
#include "RtAudio.h"

static inline DWORD dsPointerBetween(DWORD pointer, DWORD laterPointer, DWORD earlierPointer, DWORD bufferSize)
{
    if (pointer > bufferSize) pointer -= bufferSize;
    if (laterPointer < earlierPointer) laterPointer += bufferSize;
    if (pointer < earlierPointer) pointer += bufferSize;
    return pointer >= earlierPointer && pointer < laterPointer;
}

struct DsDevice {
    LPGUID id;
    bool isInput;
    std::string name;
    std::string epID; // endpoint ID

    DsDevice()
        : id(nullptr), isInput(false) {}
};

struct DsProbeData {
    bool isInput;
    std::vector<struct DsDevice>* dsDevices;
};

class RtApiDs : public RtApi
{
public:
    RtApiDs();
    ~RtApiDs();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_DS; }
    void closeStream(void) override;
    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;
    RtAudioErrorType abortStream(void) override;

    // This function is intended for internal use only.  It must be
    // public because it is called by the internal callback handler,
    // which is not a member of RtAudio.  External use of this function
    // will most likely produce highly undesirable results!
    void callbackEvent(void);

private:

    bool coInitialized_;
    bool buffersRolling = false;
    long duplexPrerollBytes = 0;
    std::vector<struct DsDevice> dsDevices_;

    void probeDevices(void) override;
    bool probeDeviceInfo(RtAudio::DeviceInfo& info, DsDevice& dsDevice);
    bool probeDeviceOpen(unsigned int deviceId, StreamMode mode, unsigned int channels,
        unsigned int firstChannel, unsigned int sampleRate,
        RtAudioFormat format, unsigned int* bufferSize,
        RtAudio::StreamOptions* options) override;
};
