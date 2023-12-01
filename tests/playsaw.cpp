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
#include <thread>

RtAudioFormat GetRtFormatFromString(const std::string& format_str) {
    if (format_str == "FLOAT32") {
        return RTAUDIO_FLOAT32;
    }
    if (format_str == "SINT8") {
        return RTAUDIO_SINT8;
    }
    if (format_str == "SINT32") {
        return RTAUDIO_SINT32;
    }
    if (format_str == "SINT16") {
        return RTAUDIO_SINT16;
    }
    if (format_str == "FLOAT64") {
        return RTAUDIO_FLOAT64;
    }
    return 0;
}

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
    RtAudioFormat format = 0;
};

int produceAudio(void* outputBuffer, void* /*inputBuffer*/, unsigned int nBufferFrames,
    double streamTime, RtAudioStreamStatus status, void* data) {
    UserData* userData = (UserData*)data;
    std::vector<void*> buffers;
    if (userData->interleaved) {
        buffers.resize(1);
        buffers[0] = outputBuffer;
    }
    else {
        buffers.resize(userData->channels);
        for (int c = 0; c < userData->channels; c++) {
            buffers[c] = ((char*)outputBuffer) + nBufferFrames * c;
        }
    }
    switch (userData->format)
    {
    case RTAUDIO_SINT8:
        fill_sin<int8_t>((int8_t**)buffers.data(), !userData->interleaved, userData->channels, userData->samplerate, nBufferFrames, userData->offset, userData->frequency, 0.8f);
        break;
    case RTAUDIO_SINT16:
        fill_sin<int16_t>((int16_t**)buffers.data(), !userData->interleaved, userData->channels, userData->samplerate, nBufferFrames, userData->offset, userData->frequency, 0.8f);
        break;
    case RTAUDIO_SINT32:
        fill_sin<int32_t>((int32_t**)buffers.data(), !userData->interleaved, userData->channels, userData->samplerate, nBufferFrames, userData->offset, userData->frequency, 0.8f);
        break;
    case RTAUDIO_FLOAT32:
        fill_sin<float>((float**)buffers.data(), !userData->interleaved, userData->channels, userData->samplerate, nBufferFrames, userData->offset, userData->frequency, 0.8f);
        break;
    case RTAUDIO_FLOAT64:
        fill_sin<double>((double**)buffers.data(), !userData->interleaved, userData->channels, userData->samplerate, nBufferFrames, userData->offset, userData->frequency, 0.8f);
        break;
    default:
        return 0;
    }
    userData->offset += nBufferFrames;
    return 0;
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
        {"format", "SINT24, SINT32, FLOAT32, FLOAT64", true, "FLOAT32"},
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
        bool thisDevice = d.name == params.getParamValue("device", argv, argc) && d.supportsOutput;
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
        if (d.supportsInput) {
            std::cout << "i";
        }
        if (d.supportsOutput) {
            std::cout << "o";
        }
        std::cout << ")" << std::endl;
    }
    std::cout << std::endl;
    if (!selectedDevice) {
        std::cout << "No device found" << std::endl;
        return 1;
    }

    unsigned int bufferFrames, fs, offset = 0, durationMs = 0, channels = 0, retries = 1;
    channels = (unsigned int)atoi(params.getParamValue("channels", argv, argc));
    fs = (unsigned int)atoi(params.getParamValue("samplerate", argv, argc));
    durationMs = atoi(params.getParamValue("time", argv, argc));
    retries = atoi(params.getParamValue("tries", argv, argc));
    bool hog = atoi(params.getParamValue("hog", argv, argc));

    auto prober = RtAudio::GetRtAudioProber(api);
    if (!prober) {
        std::cout << "\nNo prober!\n";
        return 1;
    }

    auto info = prober->probeDevice(selectedDevice->busID);
    if (!info) {
        std::cout << "\nFailed to probe device\n";
        return 1;
    }

    if (info->outputChannels == 0) {
        std::cout << "This is no output device" << std::endl;
        return 1;
    }

    if (channels == 0) {
        channels = info->outputChannels;
    }

    if (fs == 0) {
        fs = info->preferredSampleRate;
    }
    if (vector_contains(info->sampleRates, fs) == false) {
        std::cout << "Samplerate not supported" << std::endl;
        return 1;
    }
    print_device(info.value());
    std::cout << std::endl;
    std::cout << "Play samplerate: " << fs << std::endl;


    bufferFrames = atoi(params.getParamValue("buffer", argv, argc));
    auto factory = RtAudio::GetRtAudioStreamFactory(api);
    if (!factory) {
        std::cout << "\nNo factory available!\n";
        return 1;
    }
    auto format = GetRtFormatFromString(params.getParamValue("format", argv, argc));
    if (format == 0) {
        std::cout << "\nFormat is not valid!\n";
        return 1;
    }
    UserData userData;
    userData.channels = channels;
    userData.samplerate = fs;
    userData.interleaved = true;
    userData.frequency = 300;
    userData.format = format;

    RtAudio::StreamOptions options{};
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;
    if (hog) {
        options.flags |= RTAUDIO_HOG_DEVICE;
    }

    for (int iter = 0; iter < retries;) {
        CreateStreamParams params{};
        params.busId = info->partial.busID;
        params.mode = RtApi::OUTPUT;
        params.channelsInput = 0;
        params.channelsOutput = channels;
        params.sampleRate = fs;
        params.format = format;
        params.bufferSize = bufferFrames;
        params.callback = produceAudio;
        params.userData = &userData;
        params.options = &options;

        {
            auto stream = factory->createStream(params);
            if (!stream) {
                std::cout << "\nFailed to create stream!\n";
                SLEEP(0);
                continue;
            }
            std::cout << "\nStream created! Buffer size: " << stream->getBufferSize() << "\n";
            stream->startStream();
            auto start_time = std::chrono::high_resolution_clock::now();

            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
            while (stream->isStreamRunning() && elapsed_ms < durationMs) {
                elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
                SLEEP(100);
            }
            stream->stopStream();
            iter++;
        }
        SLEEP(0);
    }
    std::cout << "Finished" << std::endl;
    return 0;
}
