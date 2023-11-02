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
    void convertBuffer(const RtApi::RtApiStream stream_, char* outBuffer, char* inBuffer, RtApi::ConvertInfo info, unsigned int samples, RtApi::StreamMode mode)
    {
        typedef S24 Int24;
        typedef signed short Int16;
        typedef signed int Int32;
        typedef float Float32;
        typedef double Float64;

        // This function does format conversion, RtApi::INPUT/RtApi::OUTPUT channel compensation, and
        // data interleaving/deinterleaving.  24-bit integers are assumed to occupy
        // the lower three bytes of a 32-bit integer.

        if (stream_.deviceInterleaved[mode] != stream_.userInterleaved) {
            info.inOffset.clear();
            info.outOffset.clear();
            if ((mode == RtApi::OUTPUT && stream_.deviceInterleaved[mode]) ||
                (mode == RtApi::INPUT && stream_.userInterleaved)) {
                for (int k = 0; k < info.channels; k++) {
                    info.inOffset.push_back(k * samples);
                    info.outOffset.push_back(k);
                    info.inJump = 1;
                }
            }
            else {
                for (int k = 0; k < info.channels; k++) {
                    info.inOffset.push_back(k);
                    info.outOffset.push_back(k * samples);
                    info.outJump = 1;
                }
            }
        }
        // Clear our RtApi::DUPLEX device RtApi::OUTPUT buffer if there are more device RtApi::OUTPUTs than user RtApi::OUTPUTs
        if (outBuffer == stream_.deviceBuffer && stream_.mode == RtApi::DUPLEX && info.outJump > info.inJump)
            memset(outBuffer, 0, samples * info.outJump * RtApi::formatBytes(info.outFormat));

        int j;
        if (info.outFormat == RTAUDIO_FLOAT64) {
            Float64* out = (Float64*)outBuffer;

            if (info.inFormat == RTAUDIO_SINT8) {
                signed char* in = (signed char*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float64)in[info.inOffset[j]] / 128.0;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT16) {
                Int16* in = (Int16*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float64)in[info.inOffset[j]] / 32768.0;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT24) {
                Int24* in = (Int24*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float64)in[info.inOffset[j]].asInt() / 8388608.0;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT32) {
                Int32* in = (Int32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float64)in[info.inOffset[j]] / 2147483648.0;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT32) {
                Float32* in = (Float32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float64)in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT64) {
                // Channel compensation and/or (de)interleaving only.
                Float64* in = (Float64*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
        }
        else if (info.outFormat == RTAUDIO_FLOAT32) {
            Float32* out = (Float32*)outBuffer;

            if (info.inFormat == RTAUDIO_SINT8) {
                signed char* in = (signed char*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float32)in[info.inOffset[j]] / 128.f;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT16) {
                Int16* in = (Int16*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float32)in[info.inOffset[j]] / 32768.f;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT24) {
                Int24* in = (Int24*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float32)in[info.inOffset[j]].asInt() / 8388608.f;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT32) {
                Int32* in = (Int32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float32)in[info.inOffset[j]] / 2147483648.f;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT32) {
                // Channel compensation and/or (de)interleaving only.
                Float32* in = (Float32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT64) {
                Float64* in = (Float64*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Float32)in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
        }
        else if (info.outFormat == RTAUDIO_SINT32) {
            Int32* out = (Int32*)outBuffer;
            if (info.inFormat == RTAUDIO_SINT8) {
                signed char* in = (signed char*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)in[info.inOffset[j]];
                        out[info.outOffset[j]] <<= 24;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT16) {
                Int16* in = (Int16*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)in[info.inOffset[j]];
                        out[info.outOffset[j]] <<= 16;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT24) {
                Int24* in = (Int24*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)in[info.inOffset[j]].asInt();
                        out[info.outOffset[j]] <<= 8;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT32) {
                // Channel compensation and/or (de)interleaving only.
                Int32* in = (Int32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT32) {
                Float32* in = (Float32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        // Use llround() which returns `long long` which is guaranteed to be at least 64 bits.
                        out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 2147483648.f), 2147483647LL), -2147483648LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT64) {
                Float64* in = (Float64*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 2147483648.0), 2147483647LL), -2147483648LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
        }
        else if (info.outFormat == RTAUDIO_SINT24) {
            Int24* out = (Int24*)outBuffer;
            if (info.inFormat == RTAUDIO_SINT8) {
                signed char* in = (signed char*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)(in[info.inOffset[j]] << 16);
                        //out[info.outOffset[j]] <<= 16;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT16) {
                Int16* in = (Int16*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)(in[info.inOffset[j]] << 8);
                        //out[info.outOffset[j]] <<= 8;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT24) {
                // Channel compensation and/or (de)interleaving only.
                Int24* in = (Int24*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT32) {
                Int32* in = (Int32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)(in[info.inOffset[j]] >> 8);
                        //out[info.outOffset[j]] >>= 8;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT32) {
                Float32* in = (Float32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 8388608.f), 8388607LL), -8388608LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT64) {
                Float64* in = (Float64*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int32)std::max(std::min(std::llround(in[info.inOffset[j]] * 8388608.0), 8388607LL), -8388608LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
        }
        else if (info.outFormat == RTAUDIO_SINT16) {
            Int16* out = (Int16*)outBuffer;
            if (info.inFormat == RTAUDIO_SINT8) {
                signed char* in = (signed char*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int16)in[info.inOffset[j]];
                        out[info.outOffset[j]] <<= 8;
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT16) {
                // Channel compensation and/or (de)interleaving only.
                Int16* in = (Int16*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT24) {
                Int24* in = (Int24*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int16)(in[info.inOffset[j]].asInt() >> 8);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT32) {
                Int32* in = (Int32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int16)((in[info.inOffset[j]] >> 16) & 0x0000ffff);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT32) {
                Float32* in = (Float32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int16)std::max(std::min(std::llround(in[info.inOffset[j]] * 32768.f), 32767LL), -32768LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT64) {
                Float64* in = (Float64*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (Int16)std::max(std::min(std::llround(in[info.inOffset[j]] * 32768.0), 32767LL), -32768LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
        }
        else if (info.outFormat == RTAUDIO_SINT8) {
            signed char* out = (signed char*)outBuffer;
            if (info.inFormat == RTAUDIO_SINT8) {
                // Channel compensation and/or (de)interleaving only.
                signed char* in = (signed char*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = in[info.inOffset[j]];
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            if (info.inFormat == RTAUDIO_SINT16) {
                Int16* in = (Int16*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (signed char)((in[info.inOffset[j]] >> 8) & 0x00ff);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT24) {
                Int24* in = (Int24*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (signed char)(in[info.inOffset[j]].asInt() >> 16);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_SINT32) {
                Int32* in = (Int32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (signed char)((in[info.inOffset[j]] >> 24) & 0x000000ff);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT32) {
                Float32* in = (Float32*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (signed char)std::max(std::min(std::llround(in[info.inOffset[j]] * 128.f), 127LL), -128LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
            else if (info.inFormat == RTAUDIO_FLOAT64) {
                Float64* in = (Float64*)inBuffer;
                for (unsigned int i = 0; i < samples; i++) {
                    for (j = 0; j < info.channels; j++) {
                        out[info.outOffset[j]] = (signed char)std::max(std::min(std::llround(in[info.inOffset[j]] * 128.0), 127LL), -128LL);
                    }
                    in += info.inJump;
                    out += info.outJump;
                }
            }
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


    return RtAudioErrorType();
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
                    convertBuffer(stream_, stream_.userBuffer[RtApi::INPUT],
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
                    convertBuffer(stream_, (char*)streamBuffer,
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
