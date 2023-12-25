#include "PulseCommon.h"
#include <pulse/context.h>
#include <pulse/mainloop.h>

PaMainloop::PaMainloop()
{
    mMainloop = pa_mainloop_new();
}

bool PaMainloop::runUntil(std::function<bool()> postdicate)
{
    do {
        int retVal = 0;
        if (pa_mainloop_iterate(mMainloop, 1, &retVal) < 0) {
            return false;
        }
    } while (postdicate() == false);
    return true;
}

PaMainloop::~PaMainloop()
{
    if (mMainloop) {
        auto api = pa_mainloop_get_api(mMainloop);
        api->quit(api, 0);
        pa_mainloop_run(mMainloop, nullptr);
        pa_mainloop_free(mMainloop);
    }
}

bool PaMainloop::isValid() const
{
    return mMainloop ? true : false;
}

pa_mainloop *PaMainloop::handle() const
{
    return mMainloop;
}
