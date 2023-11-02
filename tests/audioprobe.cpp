/******************************************/
/*
  audioprobe.cpp
  by Gary P. Scavone, 2001

  Probe audio system and prints device info.
*/
/******************************************/

#include "RtAudio.h"
#include <iostream>
#include <map>

void usage(void) {
    // Error function in case of incorrect command-line
    // argument specifications
    std::cout << "\nuseage: audioprobe <apiname> <nRepeats>\n";
    std::cout << "    where apiname = an optional api (ex., 'core', default = all compiled),\n";
    std::cout << "    and nRepeats = an optional number of times to repeat the device query (default = 0),\n";
    std::cout << "                   which can be used to test device (dis)connections.\n\n";
    exit(0);
}

std::vector< RtAudio::Api > listApis()
{
    std::vector< RtAudio::Api > apis;
    RtAudio::getCompiledApi(apis);

    std::cout << "\nCompiled APIs:\n";
    for (size_t i = 0; i < apis.size(); i++)
        std::cout << i << ". " << RtAudio::getApiDisplayName(apis[i])
        << " (" << RtAudio::getApiName(apis[i]) << ")" << std::endl;

    return apis;
}

void listDevices(std::shared_ptr<RtApiEnumerator> enumerator)
{
    std::cout << "\nAPI: " << RtAudio::getApiDisplayName(enumerator->getCurrentApi()) << std::endl;

    auto devices = enumerator->listDevices();
    std::cout << "\nFound " << devices.size() << " device(s) ...\n";

    auto prober = RtAudioNamespace::GetRtAudioProber(enumerator->getCurrentApi());
    if (!prober) {
        std::cout << "\Failed to get prober" << std::endl;
        return;
    }

    for (auto& d : devices) {
        if (prober->probeDevice(d) == false) {
            std::cout << "\Failed to probe " << d.name << std::endl;
            continue;
        }
        std::cout << "\nDevice Name = " << d.name << '\n';
        std::cout << "Device Bus ID = " << d.busID << '\n';
        std::cout << "Output Channels = " << d.outputChannels << '\n';
        std::cout << "Input Channels = " << d.inputChannels << '\n';
        std::cout << "Duplex Channels = " << d.duplexChannels << '\n';
        std::cout << "Supports input: " << (d.supportsInput ? "true" : "false") << '\n';
        std::cout << "Supports output: " << (d.supportsOutput ? "true" : "false") << '\n';
        if (d.isDefaultOutput) std::cout << "This is the default output device.\n";
        else std::cout << "This is NOT the default output device.\n";
        if (d.isDefaultInput) std::cout << "This is the default input device.\n";
        else std::cout << "This is NOT the default input device.\n";
        if (d.nativeFormats == 0)
            std::cout << "No natively supported data formats(?)!";
        else {
            std::cout << "Natively supported data formats:\n";
            if (d.nativeFormats & RTAUDIO_SINT8)
                std::cout << "  8-bit int\n";
            if (d.nativeFormats & RTAUDIO_SINT16)
                std::cout << "  16-bit int\n";
            if (d.nativeFormats & RTAUDIO_SINT24)
                std::cout << "  24-bit int\n";
            if (d.nativeFormats & RTAUDIO_SINT32)
                std::cout << "  32-bit int\n";
            if (d.nativeFormats & RTAUDIO_FLOAT32)
                std::cout << "  32-bit float\n";
            if (d.nativeFormats & RTAUDIO_FLOAT64)
                std::cout << "  64-bit float\n";
        }
        if (d.sampleRates.size() < 1)
            std::cout << "No supported sample rates found!";
        else {
            std::cout << "Supported sample rates = ";
            for (unsigned int j = 0; j < d.sampleRates.size(); j++)
                std::cout << d.sampleRates[j] << " ";
        }
        std::cout << std::endl;
        if (d.preferredSampleRate == 0)
            std::cout << "No preferred sample rate found!" << std::endl;
        else
            std::cout << "Preferred sample rate = " << d.preferredSampleRate << std::endl;
    }
}

int main(int argc, char* argv[])
{
    std::cout << "\nRtAudio Version " << RtAudio::getVersion() << std::endl;

    std::vector< RtAudio::Api > apis = listApis();

    // minimal command-line checking
    if (argc > 3) usage();
    unsigned int nRepeats = 0;
    if (argc > 2) nRepeats = (unsigned int)atoi(argv[2]);

    char input;
    for (size_t api = 0; api < apis.size(); api++) {
        if (argc < 2 || apis[api] == RtAudio::getCompiledApiByName(argv[1])) {
            std::shared_ptr<RtApiEnumerator> enumerator = RtAudioNamespace::GetRtAudioEnumerator(apis[api]);
            if (!enumerator) {
                std::cout << "\nFailed to create audio enumerator" << std::endl;
                return 1;
            }
            for (size_t n = 0; n <= nRepeats; n++) {
                listDevices(enumerator);
                if (n < nRepeats) {
                    std::cout << std::endl;
                    std::cout << "\nWaiting ... press <enter> to repeat.\n";
                    std::cin.get(input);
                }
            }
        }
    }

    return 0;
}
