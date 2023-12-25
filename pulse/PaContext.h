#pragma once
#include <memory>
#include <pulse/def.h>

class PaMainloop;
struct pa_context;

class PaContext
{
public:
    PaContext(std::shared_ptr<PaMainloop> mainloop);
    ~PaContext();
    bool isValid() const;
    pa_context *handle() const;

    PaContext(const PaContext &) = delete;
    PaContext &operator=(const PaContext &) = delete;

    bool connect(const char *server);
    bool hasError() const;

    void setState(pa_context *context);
    std::shared_ptr<PaMainloop> getMainloop() const;

private:
    pa_context *mContext = NULL;
    std::shared_ptr<PaMainloop> mMainloop;
    pa_context_state mState = PA_CONTEXT_UNCONNECTED;
};
