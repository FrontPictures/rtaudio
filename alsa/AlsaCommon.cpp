#include "AlsaCommon.h"

std::string getAlsaPrettyName(snd_ctl_card_info_t* ctlinfo, snd_pcm_info_t *pcminfo){
    char name[128]{0};
    sprintf( name, "%s (%s)", snd_ctl_card_info_get_name(ctlinfo), snd_pcm_info_get_name(pcminfo) );
    return name;
}

std::string getCardIdByPCMId(std::string name){
    std::string cardName;
    for (auto& c : name){
        if (c==',')
            break;
        cardName.push_back(c);
    }
    return cardName;
}

int getCardInfoById(const char* name, snd_ctl_card_info_t *ctlinfo){
    snd_ctl_t *handle = nullptr;
    int result = 0;
    result = snd_ctl_open( &handle, name, 0 );
    if (result < 0){
        return result;
    }
    result = snd_ctl_card_info( handle, ctlinfo );
    snd_ctl_close(handle);
    return result;
}
