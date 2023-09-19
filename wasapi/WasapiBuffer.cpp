#include "WasapiBuffer.h"

WasapiBuffer::WasapiBuffer()
    : buffer_(NULL),
    bufferSize_(0),
    inIndex_(0),
    outIndex_(0) {}

WasapiBuffer::~WasapiBuffer() {
    free(buffer_);
}

void WasapiBuffer::setBufferSize(unsigned int bufferSize, unsigned int formatBytes) {
    free(buffer_);

    buffer_ = (char*)calloc(bufferSize, formatBytes);

    bufferSize_ = bufferSize;
    inIndex_ = 0;
    outIndex_ = 0;
}

bool WasapiBuffer::pushBuffer(char* buffer, unsigned int bufferSize, RtAudioFormat format)
{
    if (!buffer ||                 // incoming buffer is NULL
        bufferSize == 0 ||         // incoming buffer has no data
        bufferSize > bufferSize_) // incoming buffer too large
    {
        return false;
    }

    unsigned int relOutIndex = outIndex_;
    unsigned int inIndexEnd = inIndex_ + bufferSize;
    if (relOutIndex < inIndex_ && inIndexEnd >= bufferSize_) {
        relOutIndex += bufferSize_;
    }

    // the "IN" index CAN BEGIN at the "OUT" index
    // the "IN" index CANNOT END at the "OUT" index
    if (inIndex_ < relOutIndex && inIndexEnd >= relOutIndex) {
        return false; // not enough space between "in" index and "out" index
    }

    // copy buffer from external to internal
    int fromZeroSize = inIndex_ + bufferSize - bufferSize_;
    fromZeroSize = fromZeroSize < 0 ? 0 : fromZeroSize;
    int fromInSize = bufferSize - fromZeroSize;

    switch (format)
    {
    case RTAUDIO_SINT8:
        memcpy(&((char*)buffer_)[inIndex_], buffer, fromInSize * sizeof(char));
        memcpy(buffer_, &((char*)buffer)[fromInSize], fromZeroSize * sizeof(char));
        break;
    case RTAUDIO_SINT16:
        memcpy(&((short*)buffer_)[inIndex_], buffer, fromInSize * sizeof(short));
        memcpy(buffer_, &((short*)buffer)[fromInSize], fromZeroSize * sizeof(short));
        break;
    case RTAUDIO_SINT24:
        memcpy(&((S24*)buffer_)[inIndex_], buffer, fromInSize * sizeof(S24));
        memcpy(buffer_, &((S24*)buffer)[fromInSize], fromZeroSize * sizeof(S24));
        break;
    case RTAUDIO_SINT32:
        memcpy(&((int*)buffer_)[inIndex_], buffer, fromInSize * sizeof(int));
        memcpy(buffer_, &((int*)buffer)[fromInSize], fromZeroSize * sizeof(int));
        break;
    case RTAUDIO_FLOAT32:
        memcpy(&((float*)buffer_)[inIndex_], buffer, fromInSize * sizeof(float));
        memcpy(buffer_, &((float*)buffer)[fromInSize], fromZeroSize * sizeof(float));
        break;
    case RTAUDIO_FLOAT64:
        memcpy(&((double*)buffer_)[inIndex_], buffer, fromInSize * sizeof(double));
        memcpy(buffer_, &((double*)buffer)[fromInSize], fromZeroSize * sizeof(double));
        break;
    }

    // update "in" index
    inIndex_ += bufferSize;
    inIndex_ %= bufferSize_;

    return true;
}

bool WasapiBuffer::pullBuffer(char* buffer, unsigned int bufferSize, RtAudioFormat format)
{
    if (!buffer ||                 // incoming buffer is NULL
        bufferSize == 0 ||         // incoming buffer has no data
        bufferSize > bufferSize_) // incoming buffer too large
    {
        return false;
    }

    unsigned int relInIndex = inIndex_;
    unsigned int outIndexEnd = outIndex_ + bufferSize;
    if (relInIndex < outIndex_ && outIndexEnd >= bufferSize_) {
        relInIndex += bufferSize_;
    }

    // the "OUT" index CANNOT BEGIN at the "IN" index
    // the "OUT" index CAN END at the "IN" index
    if (outIndex_ <= relInIndex && outIndexEnd > relInIndex) {
        return false; // not enough space between "out" index and "in" index
    }

    // copy buffer from internal to external
    int fromZeroSize = outIndex_ + bufferSize - bufferSize_;
    fromZeroSize = fromZeroSize < 0 ? 0 : fromZeroSize;
    int fromOutSize = bufferSize - fromZeroSize;

    switch (format)
    {
    case RTAUDIO_SINT8:
        memcpy(buffer, &((char*)buffer_)[outIndex_], fromOutSize * sizeof(char));
        memcpy(&((char*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(char));
        break;
    case RTAUDIO_SINT16:
        memcpy(buffer, &((short*)buffer_)[outIndex_], fromOutSize * sizeof(short));
        memcpy(&((short*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(short));
        break;
    case RTAUDIO_SINT24:
        memcpy(buffer, &((S24*)buffer_)[outIndex_], fromOutSize * sizeof(S24));
        memcpy(&((S24*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(S24));
        break;
    case RTAUDIO_SINT32:
        memcpy(buffer, &((int*)buffer_)[outIndex_], fromOutSize * sizeof(int));
        memcpy(&((int*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(int));
        break;
    case RTAUDIO_FLOAT32:
        memcpy(buffer, &((float*)buffer_)[outIndex_], fromOutSize * sizeof(float));
        memcpy(&((float*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(float));
        break;
    case RTAUDIO_FLOAT64:
        memcpy(buffer, &((double*)buffer_)[outIndex_], fromOutSize * sizeof(double));
        memcpy(&((double*)buffer)[fromOutSize], buffer_, fromZeroSize * sizeof(double));
        break;
    }

    // update "out" index
    outIndex_ += bufferSize;
    outIndex_ %= bufferSize_;

    return true;
}
