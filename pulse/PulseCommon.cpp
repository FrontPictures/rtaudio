#include "PulseCommon.h"
#include <pulse/context.h>
#include <pulse/mainloop.h>

PaMainloop::PaMainloop()
{
    mMainloop = pa_mainloop_new();
}

PaMainloop::~PaMainloop()
{
    if (mMainloop)
        pa_mainloop_free(mMainloop);
}

bool PaMainloop::isValid() const
{
    return mMainloop ? true : false;
}

pa_mainloop *PaMainloop::handle() const
{
    return mMainloop;
}

PaContext::PaContext(pa_mainloop_api *api)
{
    if (!api)
        return;
    mContext = pa_context_new_with_proplist(api, NULL, NULL);
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
