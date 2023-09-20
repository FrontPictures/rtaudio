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

/*
typedef char MY_TYPE;
#define FORMAT RTAUDIO_SINT8
#define SCALE  127.0
*/

typedef signed short MY_TYPE;
#define FORMAT RTAUDIO_SINT16
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

// Platform-dependent sleep routines.
#if defined( WIN32 )
#include <windows.h>
#define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
#include <unistd.h>
#define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

// Interrupt handler function
bool done;
static void finish(int /*ignore*/) { done = true; }

#define BASE_RATE 0.005
#define TIME   1.0

void usage(void) {
    // Error function in case of incorrect command-line
    // argument specifications
    std::cout << "\nuseage: playsaw API N fs <device> <channelOffset> <time>\n";
    std::cout << "    where API = name of audio API,\n";
    std::cout << "    where N = number of channels,\n";
    std::cout << "    fs = the sample rate,\n";
    std::cout << "    device = optional device index to use (default = 0),\n";
    std::cout << "    time = an optional time duration in milliseconds (default = 1000).\n\n";
    exit(0);
}

void errorCallback(RtAudioErrorType /*type*/, const std::string& errorText)
{
    // This example error handling function simply outputs the error message to stderr.
    std::cerr << "\nerrorCallback: " << errorText << "\n\n";
}

unsigned int getDeviceIndex(std::vector<std::string> deviceNames)
{
    unsigned int i;
    std::string keyHit;
    std::cout << '\n';
    for (i = 0; i < deviceNames.size(); i++)
        std::cout << "  Device #" << i << ": " << deviceNames[i] << '\n';
    do {
        std::cout << "\nChoose a device #: ";
        std::cin >> i;
    } while (i >= deviceNames.size());
    std::getline(std::cin, keyHit);  // used to clear out stdin
    return i;
}




inline float generate_sin(int x, int samplerate, float frequency, float amplitude)
{
    const float t = 2.0 * 3.14 * frequency / samplerate;
    return amplitude * sinf(t * x);
}

template<class T>
void write_float_to_data(T** data_out, bool planar, int channels, float value, int sample,
    bool mix = false, int dst_offset = 0)
{
    T val = 0;
    if (std::is_integral_v<T>) {
        int bits_count = sizeof(T) * 8;
        uint64_t values_count = pow(2, bits_count);
        if (std::is_signed_v<T>) {
            uint64_t half_values_count = values_count / 2;
            value *= half_values_count;
        }
        else if (std::is_unsigned_v<T>) {
            value += 1;
            value *= values_count;
        }
        val = value;
    }
    else if (std::is_floating_point_v<T>) {
        val = value;
    }
    for (int c = 0; c < channels; c++) {
        if (planar) {
            if (!mix)
                data_out[c][sample + dst_offset] = val;
            else
                data_out[c][sample + dst_offset] += val;
        }
        else {
            if (!mix)
                data_out[0][channels * (sample + dst_offset) + c] = val;
            else
                data_out[0][channels * (sample + dst_offset) + c] += val;
        }
    }
}

template<class T>
void fill_sin(T** data_out_void, bool planar, int channels, int samplerate, int samples_count,
    int offset = 0, int frequency = 500, float amplitude = 0.5, bool mix = false,
    int dst_offset = 0)
{
    float y = 0;
    for (int x = 0; x < samples_count; x++) {
        y = generate_sin(x + offset, samplerate, frequency, amplitude);
        write_float_to_data(data_out_void, planar, channels, y, x, mix, dst_offset);
    }
}

struct UserData {
    int channels = 0;
    int samplerate = 0;
    bool interleaved = true;
    int offset = 0;
    int durationMs = 0;
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
    if (((float)userData->offset * 1000 / userData->samplerate) > userData->durationMs) {
        return 2;
    }
    return 0;
}

bool playsin(RtAudio& dac, unsigned int deviceId, int channels, unsigned int bufferFrames, unsigned int samplerate, bool interleaved, int durationMs) {
    dac.showWarnings(true);

    UserData userData;
    userData.channels = channels;
    userData.samplerate = samplerate;
    userData.interleaved = interleaved;
    userData.durationMs = durationMs;
    userData.frequency = 400;

    RtAudio::StreamParameters oParams;
    oParams.nChannels = channels;
    oParams.firstChannel = 0;
    oParams.deviceId = deviceId;

    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;

    while (1) {
        if (dac.openStream(&oParams, NULL, FORMAT, samplerate, &bufferFrames, &produceAudio, (void*)&userData, &options)) {
            std::cout << dac.getErrorText() << std::endl;
            SLEEP(1000);
            continue;
        }
        if (dac.isStreamOpen() == false) return false;;

        dac.startStream();

        std::cout << "\nPlaying ... (buffer size = " << bufferFrames << ").\n";

        while (dac.isStreamRunning()) SLEEP(50);
        if (dac.isStreamOpen()) dac.closeStream();
    }

    return true;
}

int main(int argc, char* argv[])
{
    unsigned int bufferFrames, fs, device = 0, offset = 0, durationMs = 100000, deviceId = 0, channels = 0;

    // minimal command-line checking
    if (argc < 4 || argc > 6) usage();

    auto api = RtAudio::getCompiledApiByName(argv[1]);
    if (api == RtAudio::UNSPECIFIED) {
        std::cout << "\nNo api found!\n";
        return 1;
    }
    std::cout << "Using API: " << RtAudio::getApiDisplayName(api) << std::endl;
    // Specify our own error callback function.
    RtAudio dac(api, &errorCallback);
    std::vector<unsigned int> deviceIds = dac.getDeviceIds();
    if (deviceIds.empty()) {
        std::cout << "\nNo audio devices found!\n";
        exit(1);
    }

    channels = (unsigned int)atoi(argv[2]);
    fs = (unsigned int)atoi(argv[3]);
    if (argc > 4)
        device = (unsigned int)atoi(argv[4]);
    if (argc > 5)
        durationMs = atoi(argv[5]);

    deviceId = deviceIds[device];
    if (device >= deviceIds.size()) {
        std::cout << "Device out of range" << std::endl;
        return 1;
    }
    auto devInfo = dac.getDeviceInfo(deviceId);

    if (fs == 0) {
        fs = devInfo.preferredSampleRate;
    }
    std::cout << "Name: " << devInfo.name << std::endl;
    std::cout << "BusID: " << devInfo.busID << std::endl;
    std::cout << "Input channels: " << devInfo.inputChannels << std::endl;
    std::cout << "Output channels: " << devInfo.outputChannels << std::endl;
    std::cout << "Native samplerate: " << devInfo.preferredSampleRate << std::endl;
    std::cout << "Play samplerate: " << fs << std::endl;

    bufferFrames = 1024;

    if (playsin(dac, deviceId, channels, bufferFrames, fs, true, durationMs) == false) {
        std::cout << "Failed to play stream" << std::endl;
        return 1;
    }

    std::cout << "Finished" << std::endl;
    return 0;
}
