#pragma once

#include "AsioCommon.h"
#include "RtAudio.h"
#include <thread>
#include <condition_variable>
#include "windowscommon.h"

void asioBufferSwitch(long index, ASIOBool /*processNow*/);
void asioSampleRateChangedGlobal(ASIOSampleRate sRate);
long asioMessagesGlobal(long selector, long value, void* /*message*/, double* /*opt*/);

class RtApiAsioStream : public RtApiStreamClass {
public:
    RtApiAsioStream(RtApi::RtApiStream stream, std::vector<ASIOBufferInfo> infos);
    RtApiAsioStream(const RtApiAsioStream&) = delete;
    ~RtApiAsioStream();
    RtAudio::Api getCurrentApi(void) override { return RtAudio::WINDOWS_ASIO; }

    RtAudioErrorType startStream(void) override;
    RtAudioErrorType stopStream(void) override;

    bool callbackEvent(long bufferIndex);
    long asioMessages(long selector, long value, void* message, double* opt);
    void sampleRateChanged(ASIOSampleRate sRate);
private:
    void deviceWatcherThread();

    std::atomic_bool asioXRun = false;
    std::vector<ASIOBufferInfo> mBufferInfos;

    std::thread mDeviceWatcherThread;
    std::atomic_int mNoAudioCallbacks;
    std::mutex mDeviceWatchMutex;

    UNIQUE_EVENT mWatchEvent;

};

extern RtApiAsioStream* apiAsioStream;