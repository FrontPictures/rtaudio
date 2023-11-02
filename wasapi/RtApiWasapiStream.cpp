#include "RtApiWasapiStream.h"
#include "utils.h"

namespace {
    typedef HANDLE(__stdcall* TAvSetMmThreadCharacteristicsPtr)(LPCWSTR TaskName, LPDWORD TaskIndex);

    void markThreadAsProAudio() {
        // Attempt to assign "Pro Audio" characteristic to thread
        HMODULE AvrtDll = LoadLibraryW(L"AVRT.dll");
        if (AvrtDll) {
            DWORD taskIndex = 0;
            TAvSetMmThreadCharacteristicsPtr AvSetMmThreadCharacteristicsPtr =
                (TAvSetMmThreadCharacteristicsPtr)(void(*)()) GetProcAddress(AvrtDll, "AvSetMmThreadCharacteristicsW");
            if (AvSetMmThreadCharacteristicsPtr) {
                AvSetMmThreadCharacteristicsPtr(L"Pro Audio", &taskIndex);
            }
            FreeLibrary(AvrtDll);
        }
    }
}

RtApiWasapiStream::RtApiWasapiStream(RtApi::RtApiStream stream, Microsoft::WRL::ComPtr<IAudioClient> audioClient,
    Microsoft::WRL::ComPtr<IAudioRenderClient> renderClient,
    Microsoft::WRL::ComPtr<IAudioCaptureClient> captureClient,
    UNIQUE_FORMAT deviceFormat, UNIQUE_EVENT streamEvent,
    AUDCLNT_SHAREMODE shareMode, RtApi::StreamMode mode) :
    RtApiStreamClass(std::move(stream)),
    mAudioClient(audioClient), mRenderClient(renderClient),
    mCaptureClient(captureClient), mDeviceFormat(std::move(deviceFormat)),
    mStreamEvent(std::move(streamEvent)), mShareMode(shareMode), mMode(mode)
{
    stream_.apiHandle;
}

RtAudioErrorType RtApiWasapiStream::startStream(void)
{
    MutexRaii<StreamMutex> lock(stream_.mutex);
    if (stream_.state != RtApi::STREAM_STOPPED) {
        if (stream_.state == RtApi::STREAM_RUNNING)
            return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is already running!");
        else if (stream_.state == RtApi::STREAM_STOPPING || stream_.state == RtApi::STREAM_CLOSED)
            return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is stopping or closed!");
        else if (stream_.state == RtApi::STREAM_ERROR)
            return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is in error state!");
        return error(RTAUDIO_WARNING, "RtApiWasapi::startStream(): the stream is not stopped!");
    }
    HRESULT hr = mAudioClient->Start();
    if (FAILED(hr)) {
        return error(RTAUDIO_SYSTEM_ERROR, "RtApiWasapi::startStream(): Unable to start stream.");
    }

    stream_.callbackInfo.thread = (ThreadHandle)CreateThread(NULL, 0, runWasapiThread, this, CREATE_SUSPENDED, NULL);
    if (!stream_.callbackInfo.thread) {
        return error(RTAUDIO_THREAD_ERROR, "RtApiWasapi::startStream: Unable to instantiate callback thread.");
    }
    if (SetThreadPriority((void*)stream_.callbackInfo.thread, stream_.callbackInfo.priority) == 0) {
        error(RTAUDIO_THREAD_ERROR, "RtApiWasapi::startStream: Unable to set thread priority.");
    }
    stream_.state = RtApi::STREAM_RUNNING;
    DWORD res = ResumeThread((void*)stream_.callbackInfo.thread);
    if (res != 1) {
        error(RTAUDIO_THREAD_ERROR, "RtApiWasapi::startStream: Unable to resume thread.");
    }
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiWasapiStream::stopStream(void)
{
    {
        MutexRaii<StreamMutex> lock(stream_.mutex);
        if (stream_.state != RtApi::STREAM_RUNNING && stream_.state != RtApi::STREAM_ERROR) {
            if (stream_.state == RtApi::STREAM_STOPPED)
                return error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): the stream is already stopped!");
            else if (stream_.state == RtApi::STREAM_CLOSED)
                return error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): the stream is closed!");
            return error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): stream is not running!");
        }
        HRESULT hr = mAudioClient->Stop();
        if (FAILED(hr)) {
            error(RTAUDIO_WARNING, "RtApiWasapi::stopStream(): failed to stop audio client.");
        }
        stream_.state = RtApi::STREAM_STOPPING;
        SetEvent(mStreamEvent.get());
    }
    WaitForSingleObject((void*)stream_.callbackInfo.thread, INFINITE);
    if (!CloseHandle((void*)stream_.callbackInfo.thread)) {
        error(RTAUDIO_THREAD_ERROR, "RtApiWasapi::stopStream: Unable to close callback thread.");
    }
    stream_.callbackInfo.thread = (ThreadHandle)NULL;
    stream_.state = RtApi::STREAM_STOPPED;
    return RTAUDIO_NO_ERROR;
}

DWORD WINAPI RtApiWasapiStream::runWasapiThread(void* wasapiPtr)
{
    if (wasapiPtr)
        ((RtApiWasapiStream*)wasapiPtr)->wasapiThread();
    return 0;
}

void RtApiWasapiStream::wasapiThread()
{
    COMLibrary_Raii comLibrary;
    HRESULT hr = S_OK;
    RtAudioCallback callback = (RtAudioCallback)stream_.callbackInfo.callback;
    markThreadAsProAudio();
    UINT32 bufferFrameCount = 0;
    {
        MutexRaii<StreamMutex> lock(stream_.mutex);
        hr = mAudioClient->GetBufferSize(&bufferFrameCount);
        if (FAILED(hr) || bufferFrameCount == 0) {
            stream_.state = RtApi::STREAM_ERROR;
            errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to get buffer size.");
            return;
        }
    }
    while (true) {
        DWORD waitResult = WaitForSingleObject(mStreamEvent.get(), 100);
        if (waitResult == WAIT_TIMEOUT) {
            MutexRaii<StreamMutex> lock(stream_.mutex);
            if (stream_.state != RtApi::STREAM_RUNNING)
                break;
        }
        {
            MutexRaii<StreamMutex> lock(stream_.mutex);
            if (stream_.state != RtApi::STREAM_RUNNING)
                break;
            if (waitResult != WAIT_OBJECT_0) {
                errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to wait event.");
                stream_.state = RtApi::STREAM_ERROR;
                break;
            }
            void* userBufferInput = nullptr;
            void* userBufferOutput = nullptr;
            UINT32 bufferFrameAvailableCount = 0;
            BYTE* streamBuffer = NULL;
            DWORD captureFlags = 0;

            if (mMode == RtApi::OUTPUT) {
                unsigned int numFramesPadding = 0;
                if (mShareMode == AUDCLNT_SHAREMODE_SHARED) {
                    hr = mAudioClient->GetCurrentPadding(&numFramesPadding);
                    if (FAILED(hr)) {
                        errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to retrieve render buffer padding.");
                        stream_.state = RtApi::STREAM_ERROR;
                        break;
                    }
                }
                bufferFrameAvailableCount = bufferFrameCount - numFramesPadding;
                if (bufferFrameAvailableCount == 0) {
                    continue;
                }

                hr = mRenderClient->GetBuffer(bufferFrameAvailableCount, &streamBuffer);
                if (FAILED(hr)) {
                    errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to retrieve render buffer.");
                    stream_.state = RtApi::STREAM_ERROR;
                    break;
                }
                if (stream_.doConvertBuffer[RtApi::StreamMode::OUTPUT]) {
                    userBufferOutput = stream_.userBuffer[RtApi::OUTPUT];
                }
                else {
                    userBufferOutput = streamBuffer;
                }
            }
            else {
                hr = mCaptureClient->GetBuffer(&streamBuffer, &bufferFrameAvailableCount, &captureFlags, NULL, NULL);
                if (FAILED(hr)) {
                    errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to get capture buffer.");
                    stream_.state = RtApi::STREAM_ERROR;
                    break;
                }
                if (bufferFrameAvailableCount == 0 || hr == AUDCLNT_S_BUFFER_EMPTY || !streamBuffer) {
                    continue;
                }
                if (stream_.doConvertBuffer[RtApi::INPUT]) {
                    userBufferInput = stream_.userBuffer[RtApi::INPUT];
                    RtApi::convertBuffer(stream_, stream_.userBuffer[RtApi::INPUT],
                        (char*)streamBuffer,
                        stream_.convertInfo[RtApi::INPUT], bufferFrameAvailableCount, RtApi::INPUT);
                }
                else {
                    userBufferInput = streamBuffer;
                }
            }

            callback(userBufferOutput,
                userBufferInput,
                bufferFrameAvailableCount,
                getStreamTime(),
                captureFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY ? RTAUDIO_INPUT_OVERFLOW : 0,
                stream_.callbackInfo.userData);
            tickStreamTime();

            if (mMode == RtApi::OUTPUT) {
                if (stream_.doConvertBuffer[mMode])
                {
                    // Convert callback buffer to stream format
                    RtApi::convertBuffer(stream_, (char*)streamBuffer,
                        stream_.userBuffer[mMode],
                        stream_.convertInfo[mMode], bufferFrameAvailableCount, mMode);
                }
                hr = mRenderClient->ReleaseBuffer(bufferFrameAvailableCount, 0);
                if (FAILED(hr)) {
                    errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to release render buffer.");
                    stream_.state = RtApi::STREAM_ERROR;
                    break;
                }
            }
            else {
                hr = mCaptureClient->ReleaseBuffer(bufferFrameAvailableCount);
                if (FAILED(hr)) {
                    errorThread(RTAUDIO_DRIVER_ERROR, "RtApiWasapi::wasapiThread: Unable to release render buffer.");
                    stream_.state = RtApi::STREAM_ERROR;
                    break;
                }
            }
        }
    }
}
