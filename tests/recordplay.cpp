#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <chrono>
#include "cliutils.h"
#include "ringbuf.hpp"
#include <thread>

using namespace std;

typedef int32_t MY_TYPE;
#define FORMAT RTAUDIO_SINT32

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
    RtAudio adc(params.api);
    adc.getDeviceInfosNoProbe();
    RtAudio::DeviceInfo info = adc.getDeviceInfoByBusID(params.busID);
    if (info.ID == 0) {
        return false;
    }

    RtAudio::StreamParameters iParams;
    iParams.nChannels = params.channels;
    iParams.firstChannel = 0;
    iParams.deviceId = info.busID;
    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    if (params.hog) {
        options.flags |= RTAUDIO_HOG_DEVICE;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < params.retries; t++) {
        if (adc.openStream(nullptr, &iParams, FORMAT, params.samplerate, &params.bufferFrames, &captureAudioCallback, params.userData, &options)) {
            std::cout << adc.getErrorText() << std::endl;
            SLEEP(500);
            continue;
        }
        adc.startStream();
        std::cout << "\nRecording... (buffer size = " << params.bufferFrames << ").\n";
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        while (adc.isStreamRunning() && elapsed_ms < params.durationMs) {
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            SLEEP(10);
        }
        adc.closeStream();
        start_time = std::chrono::high_resolution_clock::now();
    }
    return true;
}


bool playback_audio(AudioParamsCapture params) {
    RtAudio adc(params.api);
    adc.getDeviceInfosNoProbe();
    RtAudio::DeviceInfo info = adc.getDeviceInfoByBusID(params.busID);
    if (info.ID == 0) {
        return false;
    }
    RtAudio::StreamParameters oParams;
    oParams.nChannels = params.channels;
    oParams.firstChannel = 0;
    oParams.deviceId = info.busID;
    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    if (params.hog) {
        options.flags |= RTAUDIO_HOG_DEVICE;
    }
    if (adc.openStream(&oParams, nullptr, FORMAT, params.samplerate, &params.bufferFrames, &playbackAudioCallback, params.userData, &options)) {
        std::cout << adc.getErrorText() << std::endl;
        return false;
    }
    adc.startStream();
    std::cout << "\nPlayback... (buffer size = " << params.bufferFrames << ").\n";
    while (adc.isStreamRunning()) {
        SLEEP(10);
    }
    adc.closeStream();
    return true;
}

int main(int argc, char* argv[]) {
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
    RtAudio dac(api, &errorCallback);

    std::vector<RtAudio::DeviceInfo> deviceInfos = dac.getDeviceInfosNoProbe();
    deviceInfos = dac.getDeviceInfosNoProbe();
    if (deviceInfos.empty()) {
        std::cout << "\nNo audio devices found!\n";
        return 1;
    }
    std::cout << "Devices:" << std::endl;
    RtAudio::DeviceInfo selectedDeviceIn{};
    RtAudio::DeviceInfo selectedDeviceOut{};
    for (auto& d : deviceInfos) {
        bool thisDeviceIn = d.name == params.getParamValue("device_in", argv, argc);
        bool thisDeviceOut = d.name == params.getParamValue("device_out", argv, argc);
        bool nod = false;
        if (thisDeviceIn) {
            std::cout << "->";
            selectedDeviceIn = d;
            nod = true;
        }
        if (thisDeviceOut) {
            selectedDeviceOut = d;
            std::cout << "<-";
            nod = true;
        }
        if (!nod) {
            std::cout << "  ";
        }
        std::cout << d.name << std::endl;
    }
    std::cout << std::endl;
    if (selectedDeviceIn.ID == 0 || selectedDeviceOut.ID == 0) {
        std::cout << "No device found" << std::endl;
        return 1;
    }
    selectedDeviceIn = dac.getDeviceInfoByBusID(selectedDeviceIn.busID);
    if (selectedDeviceIn.ID == 0) {
        std::cout << "Failed to get input device info" << std::endl;
        return 1;
    }
    selectedDeviceOut = dac.getDeviceInfoByBusID(selectedDeviceOut.busID);
    if (selectedDeviceOut.ID == 0) {
        std::cout << "Failed to get input device info" << std::endl;
        return 1;
    }
    if (selectedDeviceOut.outputChannels == 0) {
        std::cout << "This is no output device" << std::endl;
        return 1;
    }
    if (selectedDeviceIn.inputChannels == 0) {
        std::cout << "This is no input device" << std::endl;
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
        channels = selectedDeviceIn.inputChannels;
    }

    if (fs == 0) {
        fs = selectedDeviceIn.preferredSampleRate;
    }

    if (vector_contains(selectedDeviceIn.sampleRates, fs) == false ||
        vector_contains(selectedDeviceOut.sampleRates, fs) == false) {
        std::cout << "Samplerate not supported" << std::endl;
        return 1;
    }
    cout << "Input device: " << endl << endl;
    print_device(selectedDeviceIn);

    cout << endl << "Output device: " << endl << endl;;
    print_device(selectedDeviceOut);

    UserData userData;
    userData.ringbuffer = RingBuffer<MY_TYPE>(ringbufferFill * 2);
    userData.channels = channels;
    userData.ringbufferFill = ringbufferFill;

    AudioParamsCapture paramsPass;
    paramsPass.api = api;
    paramsPass.busID = selectedDeviceOut.busID;
    paramsPass.channels = channels;
    paramsPass.bufferFrames = bufferFrames;
    paramsPass.samplerate = fs;
    paramsPass.interleaved = true;
    paramsPass.durationMs = durationMs;
    paramsPass.retries = retries;
    paramsPass.hog = hog;
    paramsPass.userData = &userData;


    std::thread play_async = std::thread(&playback_audio, paramsPass);

    paramsPass.busID = selectedDeviceIn.busID;
    std::thread capture_async = std::thread(&capture_audio, paramsPass);

    SLEEP(UINT16_MAX);
    return 0;
}
