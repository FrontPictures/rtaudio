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

struct CLIParam {
    std::string name;
    std::string description;
    bool optional = false;
    std::string default_v;
};

#include <map>

struct CLIParams {
    std::vector<CLIParam> params;
    int mOptionalParams = 0;
    int mMandatoryParams = 0;
    std::map<std::string, int> mParamToIndex;
public:

    CLIParams(std::vector<CLIParam> params_) : params(std::move(params_)) {
        bool optional = false;
        int idx = 0;
        for (auto& p : params) {
            mParamToIndex[p.name] = idx;
            idx++;
            if (p.optional) {
                optional = true;
                mOptionalParams++;
            }
            else {
                mMandatoryParams++;
            }
            if (optional && !p.optional) {
                throw std::runtime_error("Optional wrong");
            }
        }
    }
    std::string getShortString() const {
        std::stringstream ss;
        for (auto& p : params) {
            if (p.optional) {
                ss << "<";
            }
            ss << p.name;
            if (p.optional) {
                ss << ">";
            }
            ss << " ";
        }
        return ss.str();
    }
    std::string getFullString() const {
        std::stringstream ss;
        for (auto& p : params) {
            ss << "\t";
            ss << p.name;
            ss << "\t\t";
            ss << p.description;
            ss << std::endl;
        }
        return ss.str();
    }
    bool checkCountArgc(int argc) {
        argc--;
        if (argc < mMandatoryParams) {
            return false;
        }
        if (argc > mMandatoryParams + mOptionalParams) {
            return false;
        }
        return true;
    }
    const char* getParamValue(const char* name, char* argv[], int argc) {
        auto it = mParamToIndex.find(name);
        if (it == mParamToIndex.end()) {
            return nullptr;
        }
        argc--;
        if (it->second >= argc) {
            return params[it->second].default_v.c_str();
        }
        return argv[it->second + 1];
    }
};

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

bool playsin(RtAudio& dac, const RtAudio::DeviceInfo& info, int channels, unsigned int bufferFrames, unsigned int samplerate, bool interleaved, int durationMs, unsigned int retries) {
    dac.showWarnings(true);

    UserData userData;
    userData.channels = channels;
    userData.samplerate = samplerate;
    userData.interleaved = interleaved;
    userData.frequency = 400;

    RtAudio::StreamParameters oParams;
    oParams.nChannels = channels;
    oParams.firstChannel = 0;
    oParams.deviceId = info.ID;

    RtAudio::StreamOptions options;
    options.flags |= RTAUDIO_SCHEDULE_REALTIME;

    auto start_time = std::chrono::high_resolution_clock::now();
    for (int t = 0; t < retries; t++) {
        if (dac.openStream(&oParams, NULL, FORMAT, samplerate, &bufferFrames, &produceAudio, (void*)&userData, &options)) {
            std::cout << dac.getErrorText() << std::endl;
            SLEEP(100);
            continue;
        }
        if (dac.isStreamOpen() == false) return false;
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

template<class T>
bool vector_contains(const std::vector<T>& vec, const T& val) {
    for (auto& e : vec) {
        if (e == val)
            return true;
    }
    return false;
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
    deviceInfos = dac.getDeviceInfosNoProbe();
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
        std::cout << d.name << std::endl;
        if (thisDevice) {
            selectedDevice = d;
        }
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

    std::cout << "Name: " << selectedDevice.name << std::endl;
    std::cout << "BusID: " << selectedDevice.busID << std::endl;
    std::cout << "Input channels: " << selectedDevice.inputChannels << std::endl;
    std::cout << "Output channels: " << selectedDevice.outputChannels << std::endl;
    std::cout << "Native samplerate: " << selectedDevice.preferredSampleRate << std::endl;
    if (selectedDevice.nativeFormats == 0)
        std::cout << "No natively supported data formats(?)!";
    else {
        std::cout << "Natively supported data formats:\n";
        if (selectedDevice.nativeFormats & RTAUDIO_SINT8)
            std::cout << "  8-bit int\n";
        if (selectedDevice.nativeFormats & RTAUDIO_SINT16)
            std::cout << "  16-bit int\n";
        if (selectedDevice.nativeFormats & RTAUDIO_SINT24)
            std::cout << "  24-bit int\n";
        if (selectedDevice.nativeFormats & RTAUDIO_SINT32)
            std::cout << "  32-bit int\n";
        if (selectedDevice.nativeFormats & RTAUDIO_FLOAT32)
            std::cout << "  32-bit float\n";
        if (selectedDevice.nativeFormats & RTAUDIO_FLOAT64)
            std::cout << "  64-bit float\n";
    }
    if (selectedDevice.sampleRates.size() < 1)
        std::cout << "No supported sample rates found!";
    else {
        std::cout << "Supported sample rates = ";
        for (unsigned int j = 0; j < selectedDevice.sampleRates.size(); j++)
            std::cout << selectedDevice.sampleRates[j] << " ";
    }
    std::cout << std::endl;
    std::cout << "Play samplerate: " << fs << std::endl;    
    bufferFrames = atoi(params.getParamValue("buffer", argv, argc));

    if (playsin(dac, selectedDevice, channels, bufferFrames, fs, true, durationMs, retries) == false) {
        std::cout << "Failed to play stream" << std::endl;
        return 1;
    }

    std::cout << "Finished" << std::endl;
    return 0;
}
