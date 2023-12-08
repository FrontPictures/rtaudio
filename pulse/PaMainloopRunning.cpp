#include "PaMainloopRunning.h"
#include <pulse/error.h>

namespace {
void rt_pa_context_state_callback(pa_context *context, void *userdata)
{
    auto *paProbeInfo = static_cast<PaMainloopRunning *>(userdata);
    auto *userData = paProbeInfo->getUserData();
    auto state = pa_context_get_state(context);
    switch (state) {
    case PA_CONTEXT_TERMINATED:
        userData->finished(0);
        return;
    case PA_CONTEXT_FAILED:
        userData->finished(1);
        return;
    default:
        paProbeInfo->getCallback()(context, userData);
    }
}
} // namespace

PaMainloopRunningUserdata::PaMainloopRunningUserdata() {}

void PaMainloopRunningUserdata::setMainloop(pa_mainloop *ml)
{
    if (!ml)
        return;
    mApi = pa_mainloop_get_api(ml);
}

void PaMainloopRunningUserdata::finished(int code)
{
    if (!mApi)
        return;
    mApi->quit(mApi, code);
}

bool PaMainloopRunningUserdata::isValid() const
{
    return mApi != nullptr;
}

PaMainloopRunning::PaMainloopRunning(pa_mainloop *ml,
                                     pa_context *context,
                                     pa_context_notify_cb_t callback,
                                     PaMainloopRunningUserdata *userdata)
    : mMainloop(ml)
    , mContext(context)
    , mUserData(userdata)
    , mCallback(callback)
{
    pa_context_set_state_callback(context, rt_pa_context_state_callback, this);
}

RtAudioErrorType PaMainloopRunning::run()
{
    int ret = 1;

    if (pa_context_connect(mContext, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        errorStream_ << "PaMainloopRunning::run: pa_context_connect() failed: "
                     << pa_strerror(pa_context_errno(mContext));
        return error(RTAUDIO_WARNING, errorStream_.str());
    }

    if (pa_mainloop_run(mMainloop, &ret) < 0) {
        return error(RTAUDIO_WARNING, "PaMainloopRunning::run: pa_mainloop_run() failed.");
    }

    if (ret != 0) {
        return error(RTAUDIO_WARNING, "RtApiPulse::probeDevices: could not get server info.");
    }
    return RTAUDIO_NO_ERROR;
}

PaMainloopRunningUserdata *PaMainloopRunning::getUserData() const
{
    return mUserData;
}

pa_context_notify_cb_t PaMainloopRunning::getCallback() const
{
    return mCallback;
}
