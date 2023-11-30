#include "RtApiAlsaStream.h"

namespace {
void *alsaCallbackHandler(void *ptr)
{
    CallbackInfo *info = reinterpret_cast<CallbackInfo *>(ptr);
    RtApiAlsaStream *object = reinterpret_cast<RtApiAlsaStream *>(info->object);
    object->callbackEvent();
    pthread_exit(NULL);
}
} // namespace

RtApiAlsaStream::RtApiAlsaStream(RtApi::RtApiStream stream,
                                 snd_pcm_t *phandlePlayback,
                                 snd_pcm_t *phandleCapture)
    : RtApiStreamClass(std::move(stream))
    , mHandlePlayback(phandlePlayback)
    , mHandleCapture(phandleCapture)
{
    setupThread();
}

RtApiAlsaStream::~RtApiAlsaStream()
{
    {
        std::unique_lock g(mThreadMutex);
        mRunningFlag = true;
        mStopFlag = true;
        mThreadCV.notify_one();
    }

    if (stream_.callbackInfo.thread) {
        pthread_join(stream_.callbackInfo.thread, NULL);
    }

    if (mHandlePlayback) {
        snd_pcm_close(mHandlePlayback);
    }
    if (mHandleCapture) {
        snd_pcm_close(mHandleCapture);
    }
}

RtAudioErrorType RtApiAlsaStream::startStream()
{
    {
        std::unique_lock g(mThreadMutex);
        if (stream_.state != RtApi::STREAM_STOPPED) {
            return RTAUDIO_SYSTEM_ERROR;
        }
        mRunningFlag = true;
        mThreadCV.notify_one();
    }
    stream_.state = RtApi::STREAM_RUNNING;
    return RTAUDIO_NO_ERROR;
}

RtAudioErrorType RtApiAlsaStream::stopStream()
{
    {
        std::unique_lock g(mThreadMutex);
        if (stream_.state != RtApi::STREAM_RUNNING) {
            return RTAUDIO_SYSTEM_ERROR;
        }
        mRunningFlag = false;
        mThreadPausedCV.wait(g);
    }
    stream_.state = RtApi::STREAM_STOPPED;
    return RTAUDIO_NO_ERROR;
}

void RtApiAlsaStream::callbackEvent()
{
    while (true) {
        {
            std::unique_lock g(mThreadMutex);
            while (mRunningFlag == false) {
                mThreadPausedCV.notify_one();
                mThreadCV.wait(g);
            }
            if (mStopFlag)
                return;
        }
        if (processAudio() == false) {
            stream_.state = RtApi::STREAM_ERROR;
            return;
        }
    }
}

bool RtApiAlsaStream::processAudio()
{
    RtAudioStreamStatus status = 0;

    if (stream_.mode != RtApi::INPUT && mXrunOutput == true) {
        status |= RTAUDIO_OUTPUT_UNDERFLOW;
        mXrunOutput = false;
    }
    if (stream_.mode != RtApi::OUTPUT && mXrunInput == true) {
        status |= RTAUDIO_INPUT_OVERFLOW;
        mXrunInput = false;
    }
    RtAudioCallback callback = (RtAudioCallback) stream_.callbackInfo.callback;
    double streamTime = getStreamTime();

    int result;
    char *buffer;
    int channels;
    snd_pcm_t **handle;
    snd_pcm_sframes_t frames;
    RtAudioFormat format;

    if (stream_.mode == RtApi::INPUT || stream_.mode == RtApi::DUPLEX) {
        if (processInput() == false) {
            return false;
        }
    }

    callback(stream_.userBuffer[RtApi::OUTPUT].get(),
             stream_.userBuffer[RtApi::INPUT].get(),
             stream_.bufferSize,
             streamTime,
             status,
             stream_.callbackInfo.userData);

    if (stream_.mode == RtApi::OUTPUT || stream_.mode == RtApi::DUPLEX) {
        if (processOutput() == false) {
            return false;
        }
    }

    tickStreamTime();
    return true;
}

bool RtApiAlsaStream::processInput()
{
    int result = 0;
    char *buffer = nullptr;
    int channels = 0;
    snd_pcm_t *handle = mHandleCapture;
    RtAudioFormat format;

    // Setup parameters.
    if (stream_.doConvertBuffer[1]) {
        buffer = stream_.deviceBuffer.get();
        channels = stream_.nDeviceChannels[RtApi::INPUT];
        format = stream_.deviceFormat[RtApi::INPUT];
    } else {
        buffer = stream_.userBuffer[RtApi::INPUT].get();
        channels = stream_.nUserChannels[RtApi::INPUT];
        format = stream_.userFormat;
    }

    // Read samples from device in interleaved/non-interleaved format.
    int readSamples = 0;

    while (readSamples < stream_.bufferSize) {
        if (stream_.deviceInterleaved[RtApi::INPUT])
            result = snd_pcm_readi(handle,
                                   buffer + (channels * readSamples * RtApi::formatBytes(format)),
                                   stream_.bufferSize - readSamples);
        else {
            void *bufs[channels];
            size_t offset = stream_.bufferSize * RtApi::formatBytes(format);
            for (int i = 0; i < channels; i++)
                bufs[i] = (void *) (buffer + (i * offset)
                                    + (readSamples * RtApi::formatBytes(format)));
            result = snd_pcm_readn(handle, bufs, stream_.bufferSize);
        }

        if (result <= 0) {
            // Either an error or overrun occurred.
            if (result == -EPIPE) {
                snd_pcm_state_t state = snd_pcm_state(handle);
                if (state == SND_PCM_STATE_XRUN) {
                    mXrunInput = true;
                    snd_pcm_prepare(handle);
                }
                continue;
            } else if (result == -EAGAIN) {
                uint64_t bufsize64 = stream_.bufferSize - readSamples;
                usleep(bufsize64 * (1000000 / 2) / stream_.sampleRate);
                continue;
            }
            return false;
        }
        readSamples += result;
    }

    // Do byte swapping if necessary.
    if (stream_.doByteSwap[RtApi::INPUT])
        RtApi::byteSwapBuffer(buffer, readSamples * channels, format);

    // Do buffer conversion if necessary.
    if (stream_.doConvertBuffer[RtApi::INPUT])
        RtApi::convertBuffer(stream_,
                             stream_.userBuffer[RtApi::INPUT].get(),
                             stream_.deviceBuffer.get(),
                             stream_.convertInfo[RtApi::INPUT],
                             readSamples,
                             RtApi::INPUT);

    updateStreamLatency(handle, RtApi::INPUT);
    return true;
}

bool RtApiAlsaStream::processOutput()
{
    int result = 0;
    char *buffer = nullptr;
    int channels = 0;
    snd_pcm_t *handle = mHandlePlayback;
    RtAudioFormat format;

    // Setup parameters and do buffer conversion if necessary.
    if (stream_.doConvertBuffer[RtApi::OUTPUT]) {
        buffer = stream_.deviceBuffer.get();
        RtApi::convertBuffer(stream_,
                             buffer,
                             stream_.userBuffer[RtApi::OUTPUT].get(),
                             stream_.convertInfo[RtApi::OUTPUT],
                             stream_.bufferSize,
                             RtApi::OUTPUT);
        channels = stream_.nDeviceChannels[RtApi::OUTPUT];
        format = stream_.deviceFormat[RtApi::OUTPUT];
    } else {
        buffer = stream_.userBuffer[RtApi::OUTPUT].get();
        channels = stream_.nUserChannels[RtApi::OUTPUT];
        format = stream_.userFormat;
    }

    // Do byte swapping if necessary.
    if (stream_.doByteSwap[RtApi::OUTPUT])
        RtApi::byteSwapBuffer(buffer, stream_.bufferSize * channels, format);

    // Write samples to device in interleaved/non-interleaved format.
    int samplesPlayed = 0;

    while (samplesPlayed < stream_.bufferSize) {
        if (stream_.deviceInterleaved[RtApi::OUTPUT])
            result = snd_pcm_writei(handle,
                                    buffer + (samplesPlayed * RtApi::formatBytes(format) * channels),
                                    stream_.bufferSize - samplesPlayed);
        else {
            void *bufs[channels];
            size_t offset = stream_.bufferSize * RtApi::formatBytes(format);
            for (int i = 0; i < channels; i++)
                bufs[i] = (void *) (buffer + (i * offset));
            result = snd_pcm_writen(handle, bufs, stream_.bufferSize);
        }
        if (result <= 0) {
            // Either an error or underrun occurred.
            if (result == -EPIPE) {
                snd_pcm_state_t state = snd_pcm_state(handle);
                if (state == SND_PCM_STATE_XRUN) {
                    mXrunOutput = true;
                    snd_pcm_prepare(handle);
                }
                continue;
            } else if (result == -EAGAIN) {
                uint64_t bufsize64 = stream_.bufferSize - samplesPlayed;
                usleep(bufsize64 * (1000000 / 2) / stream_.sampleRate);
                continue;
            }
            return false;
        }
        samplesPlayed += result;
    }

    updateStreamLatency(handle, RtApi::OUTPUT);
    return true;
}

void RtApiAlsaStream::updateStreamLatency(snd_pcm_t *handle, RtApi::StreamMode mode)
{
    snd_pcm_sframes_t frames = 0;
    int result = snd_pcm_delay(handle, &frames);
    if (result == 0 && frames > 0)
        stream_.latency[mode] = frames;
}

bool RtApiAlsaStream::setupThread()
{
    int result = 0;
    // Setup callback thread.
    stream_.callbackInfo.object = (void *) this;

    // Set the thread attributes for joinable and realtime scheduling
    // priority (optional).  The higher priority will only take affect
    // if the program is run as root or suid. Note, under Linux
    // processes with CAP_SYS_NICE privilege, a user can change
    // scheduling policy and priority (thus need not be root). See
    // POSIX "capabilities".
    pthread_attr_t attr{};
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
#ifdef SCHED_RR // Undefined with some OSes (e.g. NetBSD 1.6.x with GNU Pthread)
    if (stream_.callbackInfo.doRealtime) {
        struct sched_param param;
        int priority = stream_.callbackInfo.priority;
        int min = sched_get_priority_min(SCHED_RR);
        int max = sched_get_priority_max(SCHED_RR);
        if (priority < min)
            priority = min;
        else if (priority > max)
            priority = max;
        param.sched_priority = priority;

        // Set the policy BEFORE the priority. Otherwise it fails.
        pthread_attr_setschedpolicy(&attr, SCHED_RR);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        // This is definitely required. Otherwise it fails.
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
        pthread_attr_setschedparam(&attr, &param);
    } else
        pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
#else
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
#endif

    stream_.callbackInfo.isRunning = true;
    result = pthread_create(&stream_.callbackInfo.thread,
                            &attr,
                            alsaCallbackHandler,
                            &stream_.callbackInfo);
    pthread_attr_destroy(&attr);
    if (result) {
        // Failed. Try instead with default attributes.
        result = pthread_create(&stream_.callbackInfo.thread,
                                NULL,
                                alsaCallbackHandler,
                                &stream_.callbackInfo);
        if (result) {
            stream_.callbackInfo.isRunning = false;
            error(RTAUDIO_THREAD_ERROR, "RtApiAlsa::error creating callback thread!");
            return RtApi::FAILURE;
        }
    }
    return RtApi::SUCCESS;
}
