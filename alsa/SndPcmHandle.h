#pragma once
#include <algorithm>
#include <alsa/asoundlib.h>

class SndPcmHandle
{
public:
    SndPcmHandle() = default;
    SndPcmHandle(const char *name, snd_pcm_stream_t stream, int mode);
    SndPcmHandle(const SndPcmHandle &) = delete;
    SndPcmHandle(SndPcmHandle &&other) { swap(*this, other); }

    ~SndPcmHandle();
    bool isValid() const;
    snd_pcm_t *handle() const;

    void swap(SndPcmHandle &first, SndPcmHandle &second) noexcept
    {
        using std::swap;
        swap(first.mHandle, second.mHandle);
    }
    SndPcmHandle &operator=(SndPcmHandle other) noexcept
    {
        swap(*this, other);
        return *this;
    }

private:
    snd_pcm_t *mHandle = nullptr;
};
