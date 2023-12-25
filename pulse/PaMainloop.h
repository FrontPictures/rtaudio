#pragma once
#include <functional>

struct pa_mainloop;

class PaMainloop
{
public:
    PaMainloop();
    ~PaMainloop();
    bool isValid() const;
    pa_mainloop *handle() const;

    PaMainloop(const PaMainloop &) = delete;
    PaMainloop &operator=(const PaMainloop &) = delete;

    bool runUntil(std::function<bool()>);

private:
    pa_mainloop *mMainloop = NULL;
};
