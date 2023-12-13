#include "RtAudio.h"
#include "cliutils.h"

void usage(const CLIParams& params) {
    // Error function in case of incorrect command-line
    // argument specifications
    std::cout << "\nuseage: defaultdevice " << params.getShortString() << "\n";
    std::cout << params.getFullString();
}

int probeDefaultDevice(std::shared_ptr<RtApiEnumerator> enumerator, std::shared_ptr<RtApiProber> prober, RtApi::StreamMode mode)
{
    auto def_device_output = enumerator->getDefaultDevice(mode);
    if (def_device_output.empty()) {
        std::cout << "Failed to get default device" << std::endl;
        return 1;
    }
    auto devInfo = prober->probeDevice(def_device_output);
    if (!devInfo) {
        std::cout << "Failed to probe device info" << std::endl;
        return 1;
    }
    print_device(devInfo.value());
    return 0;
}

int main(int argc, char* argv[]) {
    CLIParams params({
        {"api", "name of audio API", false}
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

    auto prober = RtAudio::GetRtAudioProber(api);
    if (!prober) {
        std::cout << "\nNo prober!\n";
        return 1;
    }

    std::cout << "\nGet output:\n";
    probeDefaultDevice(enumerator, prober, RtApi::StreamMode::OUTPUT);
    std::cout << "\nGet input:\n";
    probeDefaultDevice(enumerator, prober, RtApi::StreamMode::INPUT);
    return 0;
}
