#pragma once

#include <string>
#include <alsa/asoundlib.h>

std::string getAlsaPrettyName(snd_ctl_card_info_t* ctlinfo, snd_pcm_info_t *pcminfo);

std::string getCardIdByPCMId(std::string name);

int getCardInfoById(const char* name, snd_ctl_card_info_t *ctlinfo);
