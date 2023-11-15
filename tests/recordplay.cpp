#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <chrono>
#include "cliutils.h"
#include "ringbuf.hpp"
#include <thread>
#include <atomic>

using namespace std;

typedef float MY_TYPE;
#define FORMAT RTAUDIO_FLOAT32

void errorCallback(RtAudioErrorType /*type*/, const std::string& errorText)
{
    std::cerr << "\nerrorCallback: " << errorText << "\n\n";
}


void usage(const CLIParams& params) {
    std::cout << "\nuseage: recordplay " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

struct UserData {
    RingBuffer<MY_TYPE> ringbuffer;
    unsigned int channels = 0;
    unsigned int ringbufferFill = 0;
    std::mutex mut;
    bool filled = false;
};



int playbackAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
    double streamTime, RtAudioStreamStatus status, void* data) {
    UserData* userData = static_cast<UserData*>(data);
    std::lock_guard<std::mutex> g(userData->mut);
    if (userData->filled == false) {
        if (userData->ringbuffer.ReadAvailable(userData->ringbufferFill) == false) {
            memset(outputBuffer, 0, sizeof(MY_TYPE) * nBufferFrames * userData->channels);
            return 0;
        }
        userData->filled = true;
    }

    if (userData->ringbuffer.ReadAvailable(nBufferFrames * userData->channels) == false) {
        userData->filled = false;
        return 0;
    }

    userData->ringbuffer.Read(reinterpret_cast<MY_TYPE*>(outputBuffer), nBufferFrames * userData->channels);
    return 0;
}

int captureAudioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
    double streamTime, RtAudioStreamStatus status, void* data) {

    UserData* userData = static_cast<UserData*>(data);
    std::lock_guard<std::mutex> g(userData->mut);
    if (!userData->ringbuffer.WriteAvailable(nBufferFrames * userData->channels)) {
        return 0;
    }
    userData->ringbuffer.Write(reinterpret_cast<const MY_TYPE*>(inputBuffer), nBufferFrames * userData->channels);
    return 0;
}


struct AudioParamsCapture {
    RtAudio::Api api = RtAudio::Api::UNSPECIFIED;
    std::string busID;
    int channels = 0;
    unsigned int bufferFrames = 0;
    unsigned int samplerate = 0;
    bool interleaved = 0;
    int durationMs = 0;
    unsigned int retries = 0;
    bool hog = false;
    UserData* userData = nullptr;
};

bool capture_audio(AudioParamsCapture params) {
    auto factory = RtAudio::GetRtAudioStreamFactory(params.api);
    if (!factory) {
        std::cerr << "\nNo factory\n\n";
        return false;
    }
    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    if (params.hog) {
        options.flags |= RTAUDIO_HOG_DEVICE;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < params.retries; t++) {
        CreateStreamParams streamParams;
        streamParams.busId = params.busID;
        streamParams.mode = RtApi::OUTPUT;
        streamParams.channelsInput = params.channels;
        streamParams.channelsOutput = 0;
        streamParams.sampleRate = params.samplerate;
        streamParams.format = FORMAT;
        streamParams.bufferSize = params.bufferFrames;
        streamParams.callback = captureAudioCallback;
        streamParams.userData = &params.userData;
        streamParams.options = &options;

        auto stream = factory->createStream(streamParams);
        if (!stream) {
            std::cout << "\nError opening stream\n";
            SLEEP(500);
            continue;
        }
        stream->startStream();
        std::cout << "\nRecording... (buffer size = " << stream->getBufferSize() << ").\n";
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        while (stream->isStreamRunning() && elapsed_ms < params.durationMs) {
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            SLEEP(10);
        }
        start_time = std::chrono::high_resolution_clock::now();
    }
    return true;
}


bool playback_audio(AudioParamsCapture params, std::atomic_bool* stop_flag) {
    auto factory = RtAudio::GetRtAudioStreamFactory(params.api);
    if (!factory) {
        std::cerr << "\nNo factory\n\n";
        return false;
    }
    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    if (params.hog) {
        options.flags |= RTAUDIO_HOG_DEVICE;
    }
    CreateStreamParams streamParams;
    streamParams.busId = params.busID;
    streamParams.mode = RtApi::INPUT;
    streamParams.channelsInput = 0;
    streamParams.channelsOutput = params.channels;
    streamParams.sampleRate = params.samplerate;
    streamParams.format = FORMAT;
    streamParams.bufferSize = params.bufferFrames;
    streamParams.callback = playbackAudioCallback;
    streamParams.userData = &params.userData;
    streamParams.options = &options;

    auto stream = factory->createStream(streamParams);
    if (!stream) {
        std::cout << "\nError opening output stream\n";
        return false;
    }
    stream->startStream();
    std::cout << "\nPlayback... (buffer size = " << stream->getBufferSize() << ").\n";
    while (stream->isStreamRunning() && *stop_flag == false) {
        SLEEP(100);
    }
    return true;
}

int main(int argc, char* argv[]) {
    int ss = sizeof(unsigned long);
    ss = sizeof(unsigned short);
    auto res = atoi("0x11");
    return res;
    CLIParams params({
        {"api", "name of audio API", false},
        {"device_in", "input device busID to use", false},
        {"device_out", "output device busID to use", false},
        {"channels", "number of channels", true, "0"},
        {"samplerate", "the sample rate", true, "0"},
        {"buffer", "buffer frames", true, "1024"},
        {"time", "time duration in milliseconds", true, "1000"},
        {"tries", "retry count", true, "1"},
        {"hog", "hog device", true, "0"},
        {"ringbuffer", "ringbuffer fill", true, "3072"},
        });

    if (params.checkCountArgc(argc) == false) {
        usage(params);
        return 1;
    }

    auto api = RtAudio::getCompiledApiByName(params.getParamValue("api", argv, argc));
    if (api == RtAudio::UNSPECIFIED) {
        std::cout << "\nNo api found!\n";
        return 1;
    }
    std::cout << "Using API: " << RtAudio::getApiDisplayName(api) << std::endl;
    auto enumerator = RtAudio::GetRtAudioEnumerator(api);
    auto prober = RtAudio::GetRtAudioProber(api);
    if (!enumerator) {
        std::cout << "\nNo enumerator found!\n";
        return 1;
    }
    if (!prober) {
        std::cout << "\nNo prober found!\n";
        return 1;
    }

    auto devices = enumerator->listDevices();
    if (devices.empty()) {
        std::cout << "\nNo audio devices found!\n";
        return 1;
    }
    std::cout << "Devices:" << std::endl;
    std::optional<RtAudio::DeviceInfo> selectedDeviceIn{};
    std::optional<RtAudio::DeviceInfo> selectedDeviceOut{};
    for (auto& d : devices) {
        bool thisDeviceIn = d.name == params.getParamValue("device_in", argv, argc);
        bool thisDeviceOut = d.name == params.getParamValue("device_out", argv, argc);
        bool nod = false;

        RtAudio::DeviceInfo info{};
        if (thisDeviceIn || thisDeviceOut) {
            auto dev_opt = prober->probeDevice(d.busID);
            if (!dev_opt) {
                std::cout << "\nFailed to probe device " << d.name << "!\n";
                continue;
            }
            info = *dev_opt;
        }

        if (thisDeviceIn) {
            std::cout << "->";
            selectedDeviceIn = info;
            nod = true;
        }
        if (thisDeviceOut) {
            selectedDeviceOut = info;
            std::cout << "<-";
            nod = true;
        }
        if (!nod) {
            std::cout << "  ";
        }
        std::cout << d.name << std::endl;
    }
    std::cout << std::endl;
    if (!selectedDeviceIn || !selectedDeviceOut) {
        std::cout << "No device found" << std::endl;
        return 1;
    }
    if (selectedDeviceOut->outputChannels == 0) {
        std::cout << "This is no output channels" << std::endl;
        return 1;
    }
    if (selectedDeviceIn->inputChannels == 0) {
        std::cout << "This is no input channels" << std::endl;
        return 1;
    }

    unsigned int channels = (unsigned int)atoi(params.getParamValue("channels", argv, argc));
    unsigned int fs = (unsigned int)atoi(params.getParamValue("samplerate", argv, argc));
    unsigned int durationMs = atoi(params.getParamValue("time", argv, argc));
    unsigned int retries = atoi(params.getParamValue("tries", argv, argc));
    unsigned int bufferFrames = atoi(params.getParamValue("buffer", argv, argc));
    unsigned int ringbufferFill = atoi(params.getParamValue("ringbuffer", argv, argc));
    bool hog = atoi(params.getParamValue("hog", argv, argc));

    if (channels == 0) {
        channels = selectedDeviceIn->inputChannels;
    }

    if (fs == 0) {
        fs = selectedDeviceIn->preferredSampleRate;
    }

    if (vector_contains(selectedDeviceIn->sampleRates, fs) == false ||
        vector_contains(selectedDeviceOut->sampleRates, fs) == false) {
        std::cout << "Samplerate not supported" << std::endl;
        return 1;
    }
    cout << "Input device: " << endl << endl;
    print_device(*selectedDeviceIn);

    cout << endl << "Output device: " << endl << endl;;
    print_device(*selectedDeviceOut);

    UserData userData;
    userData.ringbuffer = RingBuffer<MY_TYPE>(ringbufferFill * 2);
    userData.channels = channels;
    userData.ringbufferFill = ringbufferFill;

    AudioParamsCapture paramsPass;
    paramsPass.api = api;
    paramsPass.busID = selectedDeviceOut->partial.busID;
    paramsPass.channels = channels;
    paramsPass.bufferFrames = bufferFrames;
    paramsPass.samplerate = fs;
    paramsPass.interleaved = true;
    paramsPass.durationMs = durationMs;
    paramsPass.retries = retries;
    paramsPass.hog = hog;
    paramsPass.userData = &userData;

    std::atomic_bool stop_flag = false;

    std::thread play_async = std::thread(&playback_audio, paramsPass, &stop_flag);
    paramsPass.busID = selectedDeviceIn->partial.busID;
    std::thread capture_async = std::thread(&capture_audio, paramsPass);

    capture_async.join();
    stop_flag = true;
    play_async.join();
    return 0;
}
