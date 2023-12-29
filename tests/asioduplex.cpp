#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <chrono>
#include "cliutils.h"
#include <cstring>

using namespace std;

typedef int32_t MY_TYPE;
#define FORMAT RTAUDIO_SINT32

void usage(const CLIParams& params) {
    std::cout << "\nuseage: asioduplex " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

struct UserData {
    unsigned int channels = 0;
};

int audioCallback(void *outputBuffer,
                  const void *inputBuffer,
                  unsigned int nBufferFrames,
                  double streamTime,
                  RtAudioStreamStatus status,
                  void *data)
{
    UserData* userData = static_cast<UserData*>(data);
    if (!inputBuffer || !outputBuffer)
        return 0;
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

    if (api == RtAudio::Api::UNSPECIFIED) {
        cout << "API not found" << endl;
        return 1;
    }
    if (api != RtAudio::Api::WINDOWS_ASIO && api != RtAudio::Api::LINUX_ALSA && api != RtAudio::Api::MACOSX_CORE) {
        cout << "ASIO, CORE or ALSA needed" << endl;
        return 1;
    }
    auto enumerator = RtAudio::GetRtAudioEnumerator(api);
    if (!enumerator) {
        std::cout << "\nNo enumerator!\n";
        return 1;
    }
    auto devices = enumerator->listDevices();
    if (devices.empty()) {
        std::cout << "\nNo audio devices found!\n";
        return 1;
    }

    std::cout << "Devices:" << std::endl;
    std::optional<RtAudio::DeviceInfoPartial> selectedDevice;

    for (auto& d : devices) {
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

    if (!selectedDevice) {
        std::cout << "No device found" << std::endl;
        return 1;
    }
    auto prober = RtAudio::GetRtAudioProber(api);
    if (!prober) {
        std::cout << "\nNo prober!\n";
        return 1;
    }
    auto info = prober->probeDevice(selectedDevice->busID);
    if (!info) {
        std::cout << "Failed to get input device info" << std::endl;
        return 1;
    }
    if (info->outputChannels == 0) {
        std::cout << "This is no output channels" << std::endl;
        return 1;
    }
    if (info->inputChannels == 0) {
        std::cout << "This is no input channels" << std::endl;
        return 1;
    }

    unsigned int channels = (unsigned int)atoi(params.getParamValue("channels", argv, argc));
    unsigned int fs = (unsigned int)atoi(params.getParamValue("samplerate", argv, argc));
    unsigned int durationMs = atoi(params.getParamValue("time", argv, argc));
    unsigned int retries = atoi(params.getParamValue("tries", argv, argc));
    unsigned int bufferFrames = atoi(params.getParamValue("buffer", argv, argc));

    if (channels == 0) {
        channels = info->inputChannels;
    }

    if (fs == 0) {
        fs = info->preferredSampleRate;
    }

    if (vector_contains(info->sampleRates, fs) == false) {
        std::cout << "Samplerate not supported" << std::endl;
        return 1;
    }

    cout << endl;
    print_device(*info);

    auto factory = RtAudio::GetRtAudioStreamFactory(api);
    if (!factory) {
        std::cout << "No factory" << std::endl;
        return 1;
    }

    UserData userData;
    userData.channels = channels;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < retries; t++) {
        CreateStreamParams params{};
        params.busId = info->partial.busID;
        params.mode = RtApi::DUPLEX;
        params.channelsInput = channels;
        params.channelsOutput = channels;
        params.sampleRate = fs;
        params.format = FORMAT;
        params.bufferSize = bufferFrames;
        params.callback = audioCallback;
        params.userData = &userData;
        params.options = nullptr;

        auto stream = factory->createStream(params);
        if (!stream) {
            std::cout << "\nFailed to create stream!\n";
            SLEEP(1000);
            continue;
        }
        stream->startStream();
        std::cout << "Playback... (buffer size = " << stream->getBufferSize() << ")" << endl;
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
        while (stream->isStreamRunning() && elapsed_ms < durationMs) {
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            SLEEP(10);
        }
        stream->stopStream();
        start_time = std::chrono::high_resolution_clock::now();
    }
    return 0;
}
