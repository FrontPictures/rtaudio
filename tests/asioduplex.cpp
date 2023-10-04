#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <chrono>
#include "cliutils.h"
#include "audioutils.h"
#include "ringbuf.hpp"

using namespace std;

typedef int32_t MY_TYPE;
#define FORMAT RTAUDIO_SINT32

void errorCallback(RtAudioErrorType /*type*/, const std::string& errorText)
{
    std::cerr << "\nerrorCallback: " << errorText << "\n\n";
}

void usage(const CLIParams& params) {
    std::cout << "\nuseage: asioduplex " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

struct UserData {
    unsigned int channels = 0;
};

int audioCallback(void* outputBuffer, void* inputBuffer, unsigned int nBufferFrames,
                  double streamTime, RtAudioStreamStatus status, void* data) {
    UserData* userData = static_cast<UserData*>(data);
    memcpy(outputBuffer, inputBuffer, sizeof(MY_TYPE) * userData->channels * nBufferFrames);
    return 0;
}

#include <cctype>
#include <algorithm>

int main(int argc, char* argv[]) {
    CLIParams params({
                         {"system", "asio or alsa", false},
                         {"device", "device name to use", false},
                         {"channels", "number of channels", true, "0"},
                         {"samplerate", "the sample rate", true, "0"},
                         {"buffer", "buffer frames", true, "1024"},
                         {"time", "time duration in milliseconds", true, "1000"},
                         {"tries", "retry count", true, "1"}
                     });

    if (params.checkCountArgc(argc) == false) {
        usage(params);
        return 1;
    }

    RtAudio::Api api = RtAudio::getCompiledApiByName(params.getParamValue("system", argv, argc));

    if (api == RtAudio::Api::UNSPECIFIED){
        cout<<"API not found"<<endl;
        return 1;
    }
    if (api != RtAudio::Api::WINDOWS_ASIO && api != RtAudio::Api::LINUX_ALSA){
        cout<<"ASIO or ALSA needed"<<endl;
        return 1;
    }
    RtAudio dac(api, &errorCallback);
    std::vector<RtAudio::DeviceInfo> deviceInfos = dac.getDeviceInfosNoProbe();
    deviceInfos = dac.getDeviceInfosNoProbe();
    if (deviceInfos.empty()) {
        std::cout << "\nNo audio devices found!\n";
        return 1;
    }
    std::cout << "Devices:" << std::endl;
    RtAudio::DeviceInfo selectedDevice{};
    for (auto& d : deviceInfos) {
        if (d.name == params.getParamValue("device", argv, argc)) {
            std::cout << "*";
            selectedDevice = d;
        }
        else {
            std::cout << " ";
        }
        std::cout << d.name << std::endl;
    }
    std::cout << std::endl;

    if (selectedDevice.ID == 0) {
        std::cout << "No device found" << std::endl;
        return 1;
    }
    selectedDevice = dac.getDeviceInfoByBusID(selectedDevice.busID);
    if (selectedDevice.ID == 0) {
        std::cout << "Failed to get input device info" << std::endl;
        return 1;
    }
    if (selectedDevice.outputChannels == 0) {
        std::cout << "This is no output channels" << std::endl;
        return 1;
    }
    if (selectedDevice.inputChannels == 0) {
        std::cout << "This is no input channels" << std::endl;
        return 1;
    }

    unsigned int channels = (unsigned int)atoi(params.getParamValue("channels", argv, argc));
    unsigned int fs = (unsigned int)atoi(params.getParamValue("samplerate", argv, argc));
    unsigned int durationMs = atoi(params.getParamValue("time", argv, argc));
    unsigned int retries = atoi(params.getParamValue("tries", argv, argc));
    unsigned int bufferFrames = atoi(params.getParamValue("buffer", argv, argc));

    if (channels == 0) {
        channels = selectedDevice.inputChannels;
    }

    if (fs == 0) {
        fs = selectedDevice.preferredSampleRate;
    }

    if (vector_contains(selectedDevice.sampleRates, fs) == false) {
        std::cout << "Samplerate not supported" << std::endl;
        return 1;
    }

    cout << endl;
    print_device(selectedDevice);

    RtAudio::StreamParameters oParams;
    oParams.nChannels = channels;
    oParams.firstChannel = 0;
    oParams.deviceId = selectedDevice.ID;

    RtAudio::StreamParameters iParams;
    iParams.nChannels = channels;
    iParams.firstChannel = 0;
    iParams.deviceId = selectedDevice.ID;

    UserData userData;
    userData.channels = channels;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < retries; t++) {
        if (dac.openStream(&oParams, &iParams, FORMAT, fs, &bufferFrames, &audioCallback, &userData, nullptr)) {
            std::cout << dac.getErrorText() << std::endl;
            SLEEP(500);
            continue;
        }
        dac.startStream();
        std::cout << "Playback... (buffer size = " << bufferFrames << ").\n";
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        while (dac.isStreamRunning() && elapsed_ms < durationMs) {
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            SLEEP(10);
        }

        dac.closeStream();
        start_time = std::chrono::high_resolution_clock::now();
    }

    SLEEP(UINT32_MAX);

    return 0;
}
