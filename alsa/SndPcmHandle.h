#pragma once
#include <alsa/asoundlib.h>

class SndPcmHandle
{
public:
    SndPcmHandle(const char *name, snd_pcm_stream_t stream, int mode);
    SndPcmHandle(const SndPcmHandle &) = delete;
    SndPcmHandle &operator=(const SndPcmHandle &) = delete;
    ~SndPcmHandle();
    bool isValid() const;
    snd_pcm_t *handle() const;

private:
    snd_pcm_t *mHandle = nullptr;
};
