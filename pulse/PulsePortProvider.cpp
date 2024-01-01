#include "PulsePortProvider.h"
#include "PaContextWithMainloop.h"
#include "PulseCommon.h"
#include "pulse/PaContext.h"
#include "pulse/PaMainloop.h"
#include "pulse/introspect.h"
#include <cassert>

PulsePortProvider::PulsePortProvider()
{
    mContext = PaContextWithMainloop::Create(nullptr);
    if (!mContext) {
        return;
    }
}

std::shared_ptr<PulsePortProvider> PulsePortProvider::Create()
{
    std::shared_ptr<PulsePortProvider> p = std::shared_ptr<PulsePortProvider>(
        new PulsePortProvider());
    if (p->isValid() == false)
        return {};
    return p;
}

PulsePortProvider::~PulsePortProvider() {}

namespace {

struct RtPaSuccessUserdata : public OpaqueResultError
{
public:
    void setResult(int r) { mResult = r; }
    int getResult() const { return mResult; }

private:
    int mResult = 0;
};

struct RtPaSinkInfoCallbackUserdata : public OpaqueResultError
{
    std::vector<PulseSinkSourceInfo> getInfos() const { return infos; }

    template<class T>
    bool addInfo(const T *i, int eol)
    {
        if (eol) {
            setReady();
            return true;
        }
        if (!i)
            return false;
        PulseSinkSourceInfo inf;
        inf.name = i->name;
        inf.index = i->index;
        inf.description = i->description;
        inf.driver = i->driver;
        inf.card = i->card;
        if (typeid(T) == typeid(pa_sink_info)) {
            inf.type = PulseSinkSourceType::SINK;
        } else if (typeid(T) == typeid(pa_source_info)) {
            inf.type = PulseSinkSourceType::SOURCE;
        } else {
            return false;
        }
        for (int p = 0; p < i->n_ports; p++) {
            auto ip = i->ports[p];
            PulsePortInfo port;
            port.name = ip->name;
            port.desc = ip->description;
            port.priority = ip->priority;
            if (ip->available == PA_PORT_AVAILABLE_YES
                || ip->available == PA_PORT_AVAILABLE_UNKNOWN) {
                port.available = true;
            } else {
                port.available = false;
            }
            if (i->active_port == ip) {
                port.active = true;
            } else {
                port.active = false;
            }
            inf.ports.push_back(port);
        }
        infos.push_back(inf);
        return true;
    }

private:
    std::vector<PulseSinkSourceInfo> infos;
};

struct RtPaCardInfoUserdata : public OpaqueResultError
{
public:
    std::vector<PulseCardInfo> getInfos() const { return infos; }
    bool addInfo(const pa_card_info *i, int eol)
    {
        if (eol) {
            setReady();
            return true;
        }
        if (!i)
            return false;
        PulseCardInfo info;
        info.index = i->index;
        info.name = i->name;
        info.driver = i->driver;

        for (int n = 0; n < i->n_profiles; n++) {
            auto *p = i->profiles2[n];
            PulseProfileInfo profile;
            profile.name = p->name;
            profile.description = p->description;
            profile.priority = p->priority;
            if (p == i->active_profile2) {
                profile.active = true;
            } else {
                profile.active = false;
            }
            info.profiles.push_back(profile);
        }
        infos.push_back(info);
        return true;
    }

private:
    std::vector<PulseCardInfo> infos;
};

void rt_pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto *ud = reinterpret_cast<RtPaSinkInfoCallbackUserdata *>(userdata);
    ud->addInfo(i, eol);
}

void rt_pa_card_info_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto *ud = reinterpret_cast<RtPaCardInfoUserdata *>(userdata);
    ud->addInfo(i, eol);
}

void rt_pa_source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    assert(userdata);
    auto *ud = reinterpret_cast<RtPaSinkInfoCallbackUserdata *>(userdata);
    ud->addInfo(i, eol);
}

void rt_pa_context_success_cb(pa_context *c, int success, void *userdata)
{
    assert(userdata);
    auto *r = reinterpret_cast<RtPaSuccessUserdata *>(userdata);
    r->setResult(success);
    r->setReady();
}
} // namespace

std::optional<PulseSinkSourceInfo> PulsePortProvider::getSinkSourceInfo(std::string deviceId,
                                                                        PulseSinkSourceType type)
{
    RtPaSinkInfoCallbackUserdata userd;

    pa_operation *oper = nullptr;
    if (type == PulseSinkSourceType::SINK) {
        oper = pa_context_get_sink_info_by_name(mContext->getContext()->handle(),
                                                deviceId.c_str(),
                                                rt_pa_sink_info_cb,
                                                &userd);
    } else {
        oper = pa_context_get_source_info_by_name(mContext->getContext()->handle(),
                                                  deviceId.c_str(),
                                                  rt_pa_source_info_cb,
                                                  &userd);
    }
    if (!oper)
        return {};
    mContext->getContext()->getMainloop()->runUntil([&]() {
        auto state = pa_operation_get_state(oper);
        return userd.isReady() || mContext->getContext()->hasError()
               || state != PA_OPERATION_RUNNING;
    });
    pa_operation_unref(oper);
    auto infos = userd.getInfos();
    if (infos.size() != 1) {
        return {};
    }
    auto info = infos[0];
    return info;
}

std::optional<PulseCardInfo> PulsePortProvider::getCardInfoById(uint32_t id)
{
    RtPaCardInfoUserdata userd;
    pa_operation *oper = nullptr;
    oper = pa_context_get_card_info_by_index(mContext->getContext()->handle(),
                                             id,
                                             rt_pa_card_info_cb,
                                             &userd);
    if (!oper)
        return {};
    mContext->getContext()->getMainloop()->runUntil([&]() {
        auto state = pa_operation_get_state(oper);
        return userd.isReady() || mContext->getContext()->hasError()
               || state != PA_OPERATION_RUNNING;
    });
    pa_operation_unref(oper);
    auto infos = userd.getInfos();
    if (infos.size() != 1) {
        return {};
    }
    auto info = infos[0];
    return info;
}

bool PulsePortProvider::setPortForDevice(std::string deviceId,
                                         PulseSinkSourceType type,
                                         std::string portName)
{
    RtPaSuccessUserdata userd;

    pa_operation *oper = nullptr;
    if (type == PulseSinkSourceType::SINK) {
        oper = pa_context_set_sink_port_by_name(mContext->getContext()->handle(),
                                                deviceId.c_str(),
                                                portName.c_str(),
                                                rt_pa_context_success_cb,
                                                &userd);
    } else {
        oper = pa_context_set_source_port_by_name(mContext->getContext()->handle(),
                                                  deviceId.c_str(),
                                                  portName.c_str(),
                                                  rt_pa_context_success_cb,
                                                  &userd);
    }
    if (!oper)
        return false;

    mContext->getContext()->getMainloop()->runUntil([&]() {
        auto state = pa_operation_get_state(oper);
        return userd.isReady() || mContext->getContext()->hasError()
               || state != PA_OPERATION_RUNNING;
    });
    pa_operation_unref(oper);
    if (userd.getResult() == 1)
        return true;
    return false;
}

bool PulsePortProvider::hasError() const
{
    return mContext->getContext()->hasError();
}

bool PulsePortProvider::isValid() const
{
    if (mContext)
        return true;
    return false;
}
