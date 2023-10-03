/******************************************/
/*
  playsaw.cpp
  by Gary P. Scavone, 2006-2019.

  This program will output sawtooth waveforms
  of different frequencies on each channel.
*/
/******************************************/

#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <chrono>
#include "cliutils.h"
#include "audioutils.h"
/*
typedef char MY_TYPE;
#define FORMAT RTAUDIO_SINT8
#define SCALE  127.0
*/

typedef int32_t MY_TYPE;
#define FORMAT RTAUDIO_SINT32
#define SCALE  32767.0

/*
typedef S24 MY_TYPE;
#define FORMAT RTAUDIO_SINT24
#define SCALE  8388607.0

typedef signed long MY_TYPE;
#define FORMAT RTAUDIO_SINT32
#define SCALE  2147483647.0

typedef float MY_TYPE;
#define FORMAT RTAUDIO_FLOAT32
#define SCALE  1.0

typedef double MY_TYPE;
#define FORMAT RTAUDIO_FLOAT64
#define SCALE  1.0
*/

void usage(const CLIParams& params) {
    // Error function in case of incorrect command-line
    // argument specifications
    std::cout << "\nuseage: playsaw " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

void errorCallback(RtAudioErrorType /*type*/, const std::string& errorText)
{
    // This example error handling function simply outputs the error message to stderr.
    std::cerr << "\nerrorCallback: " << errorText << "\n\n";
}

struct UserData {
    int channels = 0;
    int samplerate = 0;
    bool interleaved = true;
    int offset = 0;
    float frequency = 0;
};

int produceAudio(void* outputBuffer, void* /*inputBuffer*/, unsigned int nBufferFrames,
    double streamTime, RtAudioStreamStatus status, void* data) {
    UserData* userData = (UserData*)data;
    std::vector<MY_TYPE*> buffers;
    if (userData->interleaved) {
        buffers.resize(1);
        buffers[0] = (MY_TYPE*)outputBuffer;
    }
    else {
        buffers.resize(userData->channels);
        for (int c = 0; c < userData->channels; c++) {
            buffers[c] = ((MY_TYPE*)outputBuffer) + nBufferFrames * c;
        }
    }
    fill_sin<MY_TYPE>(buffers.data(), !userData->interleaved, userData->channels, userData->samplerate, nBufferFrames, userData->offset, userData->frequency, 0.8f);
    userData->offset += nBufferFrames;
    return 0;
}

bool playsin(RtAudio& dac, const RtAudio::DeviceInfo& info, int channels, unsigned int bufferFrames, unsigned int samplerate, bool interleaved, int durationMs, unsigned int retries, bool hog) {
    dac.showWarnings(true);

    UserData userData;
    userData.channels = channels;
    userData.samplerate = samplerate;
    userData.interleaved = interleaved;
    userData.frequency = 300;

    RtAudio::StreamParameters oParams;
    oParams.nChannels = channels;
    oParams.firstChannel = 0;
    oParams.deviceId = info.ID;

    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    if (hog) {
        options.flags |= RTAUDIO_HOG_DEVICE;
    }

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < retries; t++) {
        if (dac.openStream(&oParams, NULL, FORMAT, samplerate, &bufferFrames, &produceAudio, (void*)&userData, &options)) {
            std::cout << dac.getErrorText() << std::endl;
            SLEEP(500);
            continue;
        }        
        dac.startStream();
        std::cout << "\nPlaying ... (buffer size = " << bufferFrames << ").\n";
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        while (dac.isStreamRunning() && elapsed_ms < durationMs) {
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            SLEEP(10);
        }
        if (dac.isStreamOpen()) dac.closeStream();
        start_time = std::chrono::high_resolution_clock::now();
    }
    return true;
}

void deviceCallback(unsigned int deviceId, RtAudioDeviceParam param, void* userData) {
    return;
}

int main(int argc, char* argv[])
{
    CLIParams params({
        {"api", "name of audio API", false},
        {"device", "device busID to use", false},
        {"channels", "number of channels", true, "0"},
        {"samplerate", "the sample rate", true, "0"},
        {"buffer", "buffer frames", true, "1024"},
        {"time", "time duration in milliseconds", true, "1000"},
        {"tries", "retry count", true, "1"},
        {"hog", "hog device", true, "0"},
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
    dac.registerExtraCallback(&deviceCallback, nullptr);

    std::vector<RtAudio::DeviceInfo> deviceInfos = dac.getDeviceInfosNoProbe();
    if (deviceInfos.empty()) {
        std::cout << "\nNo audio devices found!\n";
        return 1;
    }
    std::cout << "Devices:" << std::endl;
    RtAudio::DeviceInfo selectedDevice{};
    for (auto& d : deviceInfos) {
        bool thisDevice = d.name == params.getParamValue("device", argv, argc);
        if (thisDevice) {
            std::cout << "*";
        }
        else {
            std::cout << " ";
        }
        std::cout << d.name << " (";
        if (thisDevice) {
            selectedDevice = d;
        }
        if (d.supportsInput){
            std::cout<<"i";
        }
        if (d.supportsOutput){
            std::cout<<"o";
        }
        std::cout << ")" << std::endl;
    }
    std::cout << std::endl;
    if (selectedDevice.ID == 0) {
        std::cout << "No device found" << std::endl;
        return 1;
    }

    unsigned int bufferFrames, fs, offset = 0, durationMs = 0, channels = 0, retries = 1;
    channels = (unsigned int)atoi(params.getParamValue("channels", argv, argc));
    fs = (unsigned int)atoi(params.getParamValue("samplerate", argv, argc));
    durationMs = atoi(params.getParamValue("time", argv, argc));
    retries = atoi(params.getParamValue("tries", argv, argc));
    bool hog = atoi(params.getParamValue("hog", argv, argc));

    selectedDevice = dac.getDeviceInfoByBusID(selectedDevice.busID);
    if (selectedDevice.ID == 0) {
        std::cout << "Failed to get device info" << std::endl;
        return 1;
    }

    if (selectedDevice.outputChannels == 0) {
        std::cout << "This is no output device" << std::endl;
        return 1;
    }

    if (channels == 0) {
        channels = selectedDevice.outputChannels;
    }

    if (fs == 0) {
        fs = selectedDevice.preferredSampleRate;
    }
    if (vector_contains(selectedDevice.sampleRates, fs) == false) {
        std::cout << "Samplerate not supported" << std::endl;
        return 1;
    }
    print_device(selectedDevice);
    std::cout << std::endl;
    std::cout << "Play samplerate: " << fs << std::endl;
    bufferFrames = atoi(params.getParamValue("buffer", argv, argc));

    if (playsin(dac, selectedDevice, channels, bufferFrames, fs, true, durationMs, retries, hog) == false) {
        std::cout << "Failed to play stream" << std::endl;
        return 1;
    }

    std::cout << "Finished" << std::endl;
    return 0;
}
