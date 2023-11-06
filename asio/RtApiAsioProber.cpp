#include "RtApiAsioProber.h"
#include "AsioCommon.h"

namespace {
    template<class T>
    bool vector_contains(const std::vector<T>& vec, const T& val) {
        for (auto& e : vec) {
            if (e == val)
                return true;
        }
        return false;
    }
}

std::optional<RtAudio::DeviceInfo> RtApiAsioProber::probeDevice(const std::string& busId)
{
    CLSID clsid{};
    auto c_opt = HexToCLSID(busId);
    if (!c_opt) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: unable to get class id for (" << busId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    clsid = *c_opt;

    if (!drivers.loadDriverByCLSID(clsid)) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: unable to load driver (" << busId << ").";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return {};
    }
    auto info = probeDevice2(clsid);
    ASIOExit();
    drivers.removeCurrentDriver();
    return info;
}

std::optional<RtAudio::DeviceInfo> RtApiAsioProber::probeDevice2(CLSID clsid)
{
    ASIODriverInfo driverInfo{};
    driverInfo.asioVersion = 2;
    driverInfo.sysRef = nullptr;

    char driverName[64]{};
    if (drivers.getCurrentDriverName(driverName) == false) {
        errorStream_ << "RtApiAsio::probeDevices: unable to get driver name.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    RtAudio::DeviceInfo info{};
    info.partial.name = driverName;
    info.partial.busID = CLSIDToHex(clsid);
    info.partial.supportsOutput = true;
    info.partial.supportsInput = true;

    ASIOError result = ASIOInit(&driverInfo);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") initializing driver (" << info.partial.name << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    // Determine the device channel information.
    long inputChannels, outputChannels;
    result = ASIOGetChannels(&inputChannels, &outputChannels);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") getting channel count (" << info.partial.name << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    info.outputChannels = outputChannels;
    info.inputChannels = inputChannels;
    if (info.outputChannels > 0 && info.inputChannels > 0)
        info.duplexChannels = (info.outputChannels > info.inputChannels) ? info.inputChannels : info.outputChannels;

    ASIOSampleRate currentRate = 0;
    result = ASIOGetSampleRate(&currentRate);
    if (result != ASE_OK && result != ASE_NoClock) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") get samplerate (" << info.partial.name << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }
    info.preferredSampleRate = currentRate;
    bool preferredSampleRateFound = info.preferredSampleRate ? true : false;
    for (unsigned int i = 0; i < RtAudio::MAX_SAMPLE_RATES; i++) {
        result = ASIOCanSampleRate((ASIOSampleRate)RtAudio::SAMPLE_RATES[i]);
        if (result == ASE_OK) {
            info.sampleRates.push_back(RtAudio::SAMPLE_RATES[i]);
            if (!preferredSampleRateFound) {
                if (!info.preferredSampleRate || (RtAudio::SAMPLE_RATES[i] <= 48000 && RtAudio::SAMPLE_RATES[i] > info.preferredSampleRate))
                    info.preferredSampleRate = RtAudio::SAMPLE_RATES[i];
            }
        }
    }
    if (vector_contains(info.sampleRates, info.preferredSampleRate) == false) {
        info.sampleRates.push_back(info.preferredSampleRate);
    }
    ASIOChannelInfo channelInfo;
    channelInfo.channel = 0;
    channelInfo.isInput = true;
    if (info.inputChannels <= 0) channelInfo.isInput = false;
    result = ASIOGetChannelInfo(&channelInfo);
    if (result != ASE_OK) {
        errorStream_ << "RtApiAsio::probeDeviceInfo: error (" << getAsioErrorString(result) << ") getting driver channel info (" << info.partial.name << ").";
        error(RTAUDIO_WARNING, errorStream_.str());
        return {};
    }

    info.nativeFormats = 0;
    if (channelInfo.type == ASIOSTInt16MSB || channelInfo.type == ASIOSTInt16LSB)
        info.nativeFormats |= RTAUDIO_SINT16;
    else if (channelInfo.type == ASIOSTInt32MSB || channelInfo.type == ASIOSTInt32LSB)
        info.nativeFormats |= RTAUDIO_SINT32;
    else if (channelInfo.type == ASIOSTFloat32MSB || channelInfo.type == ASIOSTFloat32LSB)
        info.nativeFormats |= RTAUDIO_FLOAT32;
    else if (channelInfo.type == ASIOSTFloat64MSB || channelInfo.type == ASIOSTFloat64LSB)
        info.nativeFormats |= RTAUDIO_FLOAT64;
    else if (channelInfo.type == ASIOSTInt24MSB || channelInfo.type == ASIOSTInt24LSB)
        info.nativeFormats |= RTAUDIO_SINT24;
    return info;
}
