#include "SndPcmHandle.h"

SndPcmHandle::SndPcmHandle(const char *name, snd_pcm_stream_t stream, int mode)
{
    snd_pcm_open(&mHandle, name, stream, mode);
}

SndPcmHandle::~SndPcmHandle()
{
    if (mHandle) {
        snd_pcm_close(mHandle);
    }
}

bool SndPcmHandle::isValid() const
{
    return mHandle ? true : false;
}

snd_pcm_t *SndPcmHandle::handle() const
{
    return mHandle;
}
