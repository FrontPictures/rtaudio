#pragma once

#include <algorithm>
struct pa_mainloop;
struct pa_context;
struct pa_mainloop_api;

class PaMainloop
{
public:
    PaMainloop();
    ~PaMainloop();
    bool isValid() const;
    pa_mainloop *handle() const;

    PaMainloop(const PaMainloop &) = delete;
    PaMainloop &operator=(const PaMainloop &) = delete;
    PaMainloop(PaMainloop &&other) { swap(*this, other); }
    PaMainloop &operator=(PaMainloop other) noexcept
    {
        swap(*this, other);
        return *this;
    }
    void swap(PaMainloop &first, PaMainloop &second) noexcept
    {
        using std::swap;
        swap(first.mMainloop, second.mMainloop);
    }

private:
    pa_mainloop *mMainloop = NULL;
};

class PaContext
{
public:
    PaContext(pa_mainloop_api *api);
    ~PaContext();
    bool isValid() const;
    pa_context *handle() const;

    PaContext(const PaContext &) = delete;
    PaContext &operator=(const PaContext &) = delete;
    PaContext(PaContext &&other) { swap(*this, other); }
    PaContext &operator=(PaContext other) noexcept
    {
        swap(*this, other);
        return *this;
    }
    void swap(PaContext &first, PaContext &second) noexcept
    {
        using std::swap;
        swap(first.mContext, second.mContext);
    }

private:
    pa_context *mContext = NULL;
};
