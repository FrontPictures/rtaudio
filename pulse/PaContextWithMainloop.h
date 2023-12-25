#pragma once
#include <memory>

class PaMainloop;
class PaContext;

class PaContextWithMainloop
{
public:
    static std::shared_ptr<PaContextWithMainloop> Create(const char *server);
    std::shared_ptr<PaContext> getContext() const;

    ~PaContextWithMainloop();

private:
    PaContextWithMainloop(const char *server);
    bool isValid() const;

    std::shared_ptr<PaMainloop> mMainloop;
    std::shared_ptr<PaContext> mContext;
    bool mValid = false;
};
