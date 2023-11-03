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
#include "cliutils.h"

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

    auto prober = RtAudio::GetRtAudioProber(enumerator->getCurrentApi());
    if (!prober) {
        std::cout << "\Failed to get prober" << std::endl;
        return;
    }

    for (auto& d : devices) {
        auto info_opt = prober->probeDevice(d.busID);
        if (!info_opt) {
            std::cout << "\Failed to probe " << d.name << std::endl;
            continue;
        }
        auto info = info_opt.value();
        std::cout << "\n\n";
        print_device(info);
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
            std::shared_ptr<RtApiEnumerator> enumerator = RtAudio::GetRtAudioEnumerator(apis[api]);
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
