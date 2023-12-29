#pragma once
#include <functional>
#include <memory>
#include <pulse/channelmap.h>
#include <pulse/def.h>
#include <pulse/sample.h>

struct pa_stream;
class PaContext;

class PaStream
{
public:
    PaStream(std::shared_ptr<PaContext> context,
             const char *streamName,
             pa_sample_spec ss,
             pa_channel_map map);
    bool connect(const char *dev, pa_buffer_attr bufAttr, bool input);
    ~PaStream();
    bool isValid() const;

    void setState(pa_stream *stream);
    void streamRequest(pa_stream *p, size_t nbytes);
    bool hasError() const;
    bool play();
    bool pause();
    void setStreamRequest(std::function<void(size_t)> req);
    bool writeData(const void *data, size_t nbytes);
    size_t peakData(const void **data);
    bool dropData();

private:
    bool tryToMoveBack();
    std::shared_ptr<PaContext> mContext;
    pa_stream *mStream = nullptr;
    pa_stream_state mState = PA_STREAM_UNCONNECTED;
    std::function<void(size_t)> mStreamRequest = nullptr;
    std::string mDeviceBusId;

    pa_buffer_attr mBufferAttr;
    bool mInput;
    int mMoveSuccess = 1;
    bool mStreamMoved = false;
};
