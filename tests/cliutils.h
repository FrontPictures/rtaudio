#pragma once
#include <string>
#include <map>

// Platform-dependent sleep routines.
#if defined( WIN32 )
#include <windows.h>
#define SLEEP( milliseconds ) Sleep( (DWORD) milliseconds ) 
#else // Unix variants
#include <unistd.h>
#define SLEEP( milliseconds ) usleep( (unsigned long) (milliseconds * 1000.0) )
#endif

struct CLIParam {
    std::string name;
    std::string description;
    bool optional = false;
    std::string default_v;
};

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

template<class T>
bool vector_contains(const std::vector<T>& vec, const T& val) {
    for (auto& e : vec) {
        if (e == val)
            return true;
    }
    return false;
}

void print_device(RtAudio::DeviceInfo selectedDevice) {
    std::cout << "Name: " << selectedDevice.partial.name << std::endl;
    std::cout << "BusID: " << selectedDevice.partial.busID << std::endl;
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
}
