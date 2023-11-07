#include "RtApiAsioEnumerator.h"
#include "include/asiodrivers.h"
#include "AsioCommon.h"

RtApiAsioEnumerator::RtApiAsioEnumerator()
{
    unsigned int nDevices = drivers.asioGetNumDev();
    if (nDevices == 0) {
        return;
    }
    char tmp[64]{};
    CLSID driver_clsid{};
    unsigned int n = 0;
    for (n = 0; n < nDevices; n++) {
        ASIOError result = drivers.asioGetDriverName((int)n, tmp, 64);
        if (result != ASE_OK) {
            errorStream_ << "RtApiAsio::probeDevices: unable to get driver name (" << getAsioErrorString(result) << ").";
            error(RTAUDIO_WARNING, errorStream_.str());
            continue;
        }
        result = drivers.asioGetDriverCLSID((int)n, &driver_clsid);
        if (result != ASE_OK) {
            errorStream_ << "RtApiAsio::probeDevices: unable to get driver class id (" << getAsioErrorString(result) << ").";
            error(RTAUDIO_WARNING, errorStream_.str());
            continue;
        }

        RtAudio::DeviceInfoPartial info;
        info.name = tmp;
        info.busID = CLSIDToHex(driver_clsid);
        info.supportsInput = true;
        info.supportsOutput = true;
        mDevices.push_back(info);
    }
}

std::vector<RtAudio::DeviceInfoPartial> RtApiAsioEnumerator::listDevices(void)
{
    return mDevices;
}
