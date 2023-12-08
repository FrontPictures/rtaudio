#pragma once
#include "RtAudio.h"
#include <pulse/context.h>
#include <pulse/mainloop.h>

class PaMainloopRunningUserdata
{
public:
    PaMainloopRunningUserdata();
    void setMainloop(pa_mainloop *ml);

    void finished(int code);
    bool isValid() const;
private:
    pa_mainloop_api *mApi = nullptr;
};

class PaMainloopRunning : public ErrorBase
{
public:
    PaMainloopRunning(pa_mainloop *ml,
                      pa_context *context,
                      pa_context_notify_cb_t callback,
                      PaMainloopRunningUserdata *userdata);
    RtAudioErrorType run();

    PaMainloopRunningUserdata *getUserData() const;
    pa_context_notify_cb_t getCallback() const;

private:
    pa_context_notify_cb_t mCallback = nullptr;
    PaMainloopRunningUserdata *mUserData = nullptr;
    pa_mainloop *mMainloop = nullptr;
    pa_context *mContext = nullptr;
};
