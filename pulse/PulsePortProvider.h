#pragma once
#include "PulseDataStructs.h"
#include "RtAudio.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class PaContextWithMainloop;

class RTAUDIO_DLL_PUBLIC PulsePortProvider
{
public:
    static std::shared_ptr<PulsePortProvider> Create();

    ~PulsePortProvider();
    std::optional<PulseSinkSourceInfo> getSinkSourceInfo(std::string deviceId,
                                                         PulseSinkSourceType type);
    std::optional<PulseCardInfo> getCardInfoById(uint32_t id);
    std::optional<std::vector<PulseCardInfo>> getCards();

    bool setPortForDevice(std::string deviceId, PulseSinkSourceType type, std::string portName);
    bool hasError() const;

private:
    PulsePortProvider();
    bool isValid() const;
    std::shared_ptr<PaContextWithMainloop> mContext;
};
