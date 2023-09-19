#pragma once

#include "RtAudio.h"
#include <mftransform.h>
#include <wmcodecdsp.h>

// In order to satisfy WASAPI's buffer requirements, we need a means of converting sample rate
// between HW and the user. The WasapiResampler class is used to perform this conversion between
// HwIn->UserIn and UserOut->HwOut during the stream callback loop.

class WasapiResampler
{
public:
    WasapiResampler(bool isFloat, unsigned int bitsPerSample, unsigned int channelCount,
        unsigned int inSampleRate, unsigned int outSampleRate);

    ~WasapiResampler();

    void Convert(char* outBuffer, const char* inBuffer, unsigned int inSampleCount, unsigned int& outSampleCount, int maxOutSampleCount = -1);

private:
    unsigned int _bytesPerSample;
    unsigned int _channelCount;
    float _sampleRatio;

    IUnknown* _transformUnk;
    IMFTransform* _transform;
    IMFMediaType* _mediaType;
    IMFMediaType* _inputMediaType;
    IMFMediaType* _outputMediaType;

#ifdef __IWMResamplerProps_FWD_DEFINED__
    IWMResamplerProps* _resamplerProps;
#endif
};
