#include "PaStream.h"
#include "pulse/PaContext.h"
#include "pulse/PaMainloop.h"
#include "pulse/PulseCommon.h"
#include <cassert>
#include <map>
#include <pulse/introspect.h>
#include <pulse/stream.h>

namespace {

static std::map<const void *, bool *> mFreeHandles;

void rt_pa_stream_notify_cb(pa_stream *p, void *userdata)
{
    assert(userdata);
    auto *stream = reinterpret_cast<PaStream *>(userdata);
    stream->setState(p);
}

void rt_pa_stream_success_cb(pa_stream *s, int success, void *userdata)
{
    assert(userdata);
    auto *successOut = reinterpret_cast<int *>(userdata);
    (*successOut) = success;
}
void rt_pa_context_success_cb(pa_context *c, int success, void *userdata)
{
    assert(userdata);
    auto *successOut = reinterpret_cast<int *>(userdata);
    (*successOut) = success;
}

void rt_pa_stream_request_cb(pa_stream *p, size_t nbytes, void *userdata)
{
    assert(userdata);
    auto *stream = reinterpret_cast<PaStream *>(userdata);
    stream->streamRequest(p, nbytes);
}

void rt_pa_free_cb(void *p)
{
    assert(p);
    auto it = mFreeHandles.find(p);
    if (it == mFreeHandles.end()) {
        assert(false);
        return;
    }
    *(it->second) = true;
}
} // namespace

PaStream::PaStream(std::shared_ptr<PaContext> context,
                   const char *streamName,
                   pa_sample_spec ss,
                   pa_channel_map map)
    : mContext(context)
{
    if (!context)
        return;
    mStream = pa_stream_new(context->handle(), streamName, &ss, &map);
    if (!mStream) {
        return;
    }
    pa_stream_set_state_callback(mStream, rt_pa_stream_notify_cb, this);
    pa_stream_set_write_callback(mStream, rt_pa_stream_request_cb, this);
    pa_stream_set_read_callback(mStream, rt_pa_stream_request_cb, this);
    pa_stream_set_moved_callback(mStream, rt_pa_stream_notify_cb, this);
}

bool PaStream::connect(const char *dev, pa_buffer_attr bufAttr, bool input)
{
    if (!isValid())
        return false;
    mDeviceBusId = dev;
    mBufferAttr = bufAttr;
    mInput = input;
    auto loop = mContext->getMainloop();
    if (!loop) {
        return false;
    }
    int flags = PA_STREAM_START_CORKED | PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE;
    if (input) {
        if (pa_stream_connect_record(mStream, dev, &bufAttr, (pa_stream_flags) flags) != 0) {
            return false;
        }
    } else {
        if (pa_stream_connect_playback(mStream,
                                       dev,
                                       &bufAttr,
                                       (pa_stream_flags) flags,
                                       nullptr,
                                       nullptr)
            != 0) {
            return false;
        }
    }
    if (loop->runUntil(
            [this]() { return mState == PA_STREAM_READY || hasError() || mContext->hasError(); })
        == false) {
        return false;
    }
    if (mState != PA_STREAM_READY)
        return false;
    return true;
}

PaStream::~PaStream()
{
    if (mStream) {
        return;
    }
    pa_stream_disconnect(mStream);
    pa_stream_set_state_callback(mStream, nullptr, this);
    pa_stream_set_write_callback(mStream, nullptr, this);
    pa_stream_set_read_callback(mStream, nullptr, this);
    pa_stream_set_moved_callback(mStream, nullptr, this);
    pa_stream_unref(mStream);
}

bool PaStream::isValid() const
{
    return mStream;
}

void PaStream::setState(pa_stream *stream)
{
    assert(stream == mStream);
    mState = pa_stream_get_state(mStream);
    auto devName = pa_stream_get_device_name(mStream);
    if (!devName)
        return;
    if (devName != mDeviceBusId) {
        mStreamMoved = true;
    }
}

void PaStream::streamRequest(pa_stream *p, size_t nbytes)
{
    assert(mStream == p);
    auto loop = mContext->getMainloop();
    if (!mStreamRequest) {
        void *silence = calloc(nbytes, 1);
        bool success = false;
        writeData(silence, nbytes);
        free(silence);
        return;
    }
    mStreamRequest(nbytes);
}

bool PaStream::hasError() const
{
    return !(PA_STREAM_IS_GOOD(mState)) || mStreamMoved || mMoveSuccess == 0;
}

bool PaStream::play()
{
    if (!isValid())
        return false;
    auto loop = mContext->getMainloop();
    if (!loop) {
        return false;
    }

    int corked = pa_stream_is_corked(mStream);
    if (corked < 0)
        return false;
    if (corked == 0)
        return true;
    int success = 100;
    pa_operation *oper = pa_stream_cork(mStream, 0, rt_pa_stream_success_cb, &success);
    if (!oper)
        return false;
    loop->runUntil([&]() { return success != 100 || mContext->hasError(); });
    pa_operation_unref(oper);
    if (success == 1)
        return true;
    return false;
}

bool PaStream::pause()
{
    if (!isValid())
        return false;
    auto loop = mContext->getMainloop();
    if (!loop) {
        return false;
    }

    int corked = pa_stream_is_corked(mStream);
    if (corked < 0)
        return false;
    if (corked == 1)
        return true;
    int success = 100;
    pa_operation *oper = pa_stream_cork(mStream, 1, rt_pa_stream_success_cb, &success);
    if (!oper)
        return false;
    loop->runUntil([&]() { return success != 100 || mContext->hasError(); });
    pa_operation_unref(oper);
    if (success == 1)
        return true;
    return false;
}

void PaStream::setStreamRequest(std::function<void(size_t)> req)
{
    mStreamRequest = req;
}

bool PaStream::writeData(const void *data, size_t nbytes)
{
    if (!isValid())
        return false;
    if (!data || nbytes == 0)
        return false;
    auto loop = mContext->getMainloop();
    if (!loop) {
        return false;
    }

    bool success = false;
    mFreeHandles.insert(std::make_pair(data, &success));
    auto res = pa_stream_write(mStream, data, nbytes, rt_pa_free_cb, 0, PA_SEEK_RELATIVE);
    mFreeHandles.erase(data);
    if (res != 0 || success == false) {
        return false;
    }
    return true;
}

size_t PaStream::peakData(const void **data)
{
    size_t nbytes = 0;
    if (pa_stream_peek(mStream, data, &nbytes) != 0) {
        return 0;
    }
    return nbytes;
}

bool PaStream::dropData()
{
    if (pa_stream_drop(mStream) != 0) {
        return false;
    }
    return true;
}

bool PaStream::tryToMoveBack()
{
    auto loop = mContext->getMainloop();
    if (!loop) {
        return false;
    }

    auto idx = pa_stream_get_index(mStream);
    pa_operation *oper = pa_context_move_sink_input_by_name(mContext->handle(),
                                                            idx,
                                                            mDeviceBusId.c_str(),
                                                            rt_pa_context_success_cb,
                                                            &mMoveSuccess);
    if (!oper)
        return false;
    pa_operation_unref(oper);
    return true;
}
