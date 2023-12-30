#pragma once
#include "PulseCommon.h"
#include <memory>
#include <string>
#include <vector>

class PaContextWithMainloop;
class PaMainloop;

class RTAUDIO_DLL_PUBLIC PulsePortProvider
{
public:
    static std::shared_ptr<PulsePortProvider> Create();

    ~PulsePortProvider();
    std::optional<PulseSinkSourceInfo> getSinkSourceInfo(std::string deviceId,
                                                         PulseSinkSourceType type);
    std::optional<PulseCardInfo> getCardInfoById(uint32_t id);

    bool setPortForDevice(std::string deviceId, PulseSinkSourceType type, std::string portName);
    bool hasError() const;

private:
    PulsePortProvider();
    bool isValid() const;
    std::shared_ptr<PaContextWithMainloop> mContext;
};
