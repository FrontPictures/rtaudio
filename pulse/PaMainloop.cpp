#include "PaMainloop.h"
#include <cstdint>
#include <cstdio>
#include <pulse/mainloop.h>

PaMainloop::PaMainloop()
{
    mMainloop = pa_mainloop_new();
}

bool PaMainloop::runUntil(std::function<bool()> postdicate)
{
    if (!isValid())
        return false;
    do {
        int retVal = 0;
        if (pa_mainloop_iterate(mMainloop, 1, &retVal) < 0) {
            mErrorWhileRunning = true;
            return false;
        }        
    } while (postdicate() == false);
    return true;
}

bool PaMainloop::iterateBlocking()
{
    if (!isValid())
        return false;
    int retVal = 0;
    if (pa_mainloop_iterate(mMainloop, 1, &retVal) < 0) {
        mErrorWhileRunning = true;
        return false;
    }
    return true;
}

bool PaMainloop::stop()
{
    if (!isValid())
        return false;
    pa_mainloop_quit(mMainloop, 0);
    return true;
}

PaMainloop::~PaMainloop()
{
    if (!isValid())
        return;
    stop();
    if (mErrorWhileRunning == false) {
        pa_mainloop_run(mMainloop, nullptr);
    }
    pa_mainloop_free(mMainloop);
    mMainloop = nullptr;
}

bool PaMainloop::isValid() const
{
    return mMainloop ? true : false;
}

pa_mainloop *PaMainloop::handle() const
{
    return mMainloop;
}
