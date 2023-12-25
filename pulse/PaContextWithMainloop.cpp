#include "PaContextWithMainloop.h"
#include "pulse/PaContext.h"
#include "pulse/PaMainloop.h"

std::shared_ptr<PaContextWithMainloop> PaContextWithMainloop::Create(const char *server)
{
    auto context = std::shared_ptr<PaContextWithMainloop>(new PaContextWithMainloop(server));
    if (context->isValid() == false) {
        return {};
    }
    return context;
}

std::shared_ptr<PaContext> PaContextWithMainloop::getContext() const
{
    return mContext;
}

PaContextWithMainloop::PaContextWithMainloop(const char *server)
{
    mMainloop = std::make_shared<PaMainloop>();
    if (mMainloop->isValid() == false) {
        return;
    }

    mContext = std::make_shared<PaContext>(mMainloop);
    if (mContext->isValid() == false) {
        return;
    }

    if (mContext->connect(server) == false) {
        return;
    }
    mValid = true;
}

PaContextWithMainloop::~PaContextWithMainloop() {}

bool PaContextWithMainloop::isValid() const
{
    return mValid;
}
