#include "PaContext.h"
#include "PaMainloop.h"
#include "PulseCommon.h"
#include <cassert>
#include <pulse/context.h>
#include <pulse/mainloop.h>

namespace {
void rt_pa_context_state_callback(pa_context *context, void *userdata)
{
    auto *paProbeInfo = static_cast<PaContext *>(userdata);
    paProbeInfo->setState(context);
}
} // namespace

PaContext::PaContext(std::shared_ptr<PaMainloop> mainloop)
    : mMainloop(mainloop)
{
    if (!mainloop)
        return;
    auto api = pa_mainloop_get_api(mMainloop->handle());
    if (!api)
        return;
    mContext = pa_context_new_with_proplist(api, NULL, NULL);
    if (!mContext)
        return;
    pa_context_set_state_callback(mContext, rt_pa_context_state_callback, this);
}

bool PaContext::connect(const char *server)
{
    if (!isValid())
        return false;
    if (pa_context_connect(mContext, server, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        return false;
    }
    mState = PA_CONTEXT_CONNECTING;
    if (mMainloop->runUntil([this]() {
            if (hasError() || mState == PA_CONTEXT_READY)
                return true;
            return false;
        })
        == false) {
        return false;
    }
    if (hasError()) {
        return false;
    }
    return true;
}

bool PaContext::hasError() const
{
    return !(PA_CONTEXT_IS_GOOD(mState));
}

void PaContext::setState(pa_context *context)
{
    assert(context == mContext);
    mState = pa_context_get_state(context);
}

std::shared_ptr<PaMainloop> PaContext::getMainloop() const
{
    return mMainloop;
}

PaContext::~PaContext()
{
    if (mContext)
        pa_context_unref(mContext);
}

bool PaContext::isValid() const
{
    return mContext ? true : false;
}

pa_context *PaContext::handle() const
{
    return mContext;
}
