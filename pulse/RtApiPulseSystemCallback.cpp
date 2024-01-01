#include "RtApiPulseSystemCallback.h"
#include "PaContextWithMainloop.h"
#include "PaMainloop.h"
#include "PulseCommon.h"
#include "pulse/PaContext.h"
#include <pulse/context.h>
#include <pulse/def.h>
#include <pulse/mainloop.h>
#include <pulse/pulseaudio.h>

namespace {

void rt_pa_context_success_cb(pa_context *c, int success, void *userdata)
{
    assert(userdata);
    int *res = reinterpret_cast<int *>(userdata);
    (*res) = success;
}

void pa_context_subscribe_cb(pa_context *c,
                             pa_subscription_event_type_t t,
                             uint32_t idx,
                             void *userdata)
{
    assert(userdata);
    RtApiPulseSystemCallback *stream = reinterpret_cast<RtApiPulseSystemCallback *>(userdata);
    stream->handleEvent(c, t, idx);
}

enum PulseSinkSourceType getPulseSinkSourceTypeByFacility(unsigned facility)
{
    switch (facility) {
    case PA_SUBSCRIPTION_EVENT_SINK:
        return PulseSinkSourceType::SINK;
    case PA_SUBSCRIPTION_EVENT_SOURCE: {
        return PulseSinkSourceType::SOURCE;
    } break;
    }
    assert(false);
    return PulseSinkSourceType::SOURCE;
}

RtAudioDeviceParam getRtAudioDeviceParamByPulseEvent(unsigned evt)
{
    switch (evt) {
    case PA_SUBSCRIPTION_EVENT_CHANGE:
        return RtAudioDeviceParam::DEVICE_STATE_CHANGED;
    case PA_SUBSCRIPTION_EVENT_NEW:
        return RtAudioDeviceParam::DEVICE_ADDED;
    case PA_SUBSCRIPTION_EVENT_REMOVE:
        return RtAudioDeviceParam::DEVICE_REMOVED;
    }
    assert(false);
    return RtAudioDeviceParam::DEVICE_STATE_CHANGED;
};
} // namespace

RtApiPulseSystemCallback::RtApiPulseSystemCallback(RtAudioDeviceCallbackLambda callback)
    : mCallback(callback)
{
    mContextWithLoop = PaContextWithMainloop::Create(nullptr);
    if (!mContextWithLoop) {
        errorStream_ << "RtApiPulse::probeDevices: failed to create context with mainloop.";
        error(RTAUDIO_SYSTEM_ERROR, errorStream_.str());
        return;
    }
    pa_context_set_subscribe_callback(mContextWithLoop->getContext()->handle(),
                                      pa_context_subscribe_cb,
                                      this);

    int success = 100;
    pa_operation *operation = pa_context_subscribe(mContextWithLoop->getContext()->handle(),
                                                   static_cast<pa_subscription_mask_t>(
                                                       PA_SUBSCRIPTION_MASK_ALL),
                                                   &rt_pa_context_success_cb,
                                                   &success);
    if (!operation)
        return;
    mContextWithLoop->getContext()->getMainloop()->runUntil(
        [&]() { return mContextWithLoop->getContext()->hasError() || success != 100; });
    pa_operation_unref(operation);
    if (success != 1) {
        return;
    }
    mHasError = false;
    mNotificationThread = std::thread(&RtApiPulseSystemCallback::notificationThread, this);
}

RtApiPulseSystemCallback::~RtApiPulseSystemCallback()
{
    if (!mContextWithLoop) {
        return;
    }
    mContextWithLoop->getContext()->getMainloop()->stop();
    mNotificationThread.join();
    pa_context_set_subscribe_callback(mContextWithLoop->getContext()->handle(), nullptr, this);
}

void RtApiPulseSystemCallback::handleEvent(pa_context *c,
                                           pa_subscription_event_type_t t,
                                           uint32_t idx)
{
    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    switch (facility) {
    case PA_SUBSCRIPTION_EVENT_SINK:
    case PA_SUBSCRIPTION_EVENT_SOURCE: {
        checkDefaultChanged(c, idx, t);
    } break;
    default:
        break;
    }
}

bool RtApiPulseSystemCallback::hasError() const
{
    return mHasError;
}

void RtApiPulseSystemCallback::notificationThread()
{
    mContextWithLoop->getContext()->getMainloop()->runUntil(
        [this]() { return mContextWithLoop->getContext()->hasError(); });
    mHasError = true;
}

void RtApiPulseSystemCallback::checkDefaultChanged(pa_context *c, uint32_t idx, unsigned int t)
{
    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    unsigned int evt = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    switch (evt) {
    case PA_SUBSCRIPTION_EVENT_CHANGE:
    case PA_SUBSCRIPTION_EVENT_NEW: {
        PulseCommon::getSinkSourceInfoAsync(mContextWithLoop->getContext(),
                                            idx,
                                            getPulseSinkSourceTypeByFacility(facility),
                                            [evt, this](std::optional<PulseSinkSourceInfo> info) {
                                                if (!info) {
                                                    return;
                                                }
                                                mCallback(info->name,
                                                          getRtAudioDeviceParamByPulseEvent(evt));
                                            });
    } break;
    case PA_SUBSCRIPTION_EVENT_REMOVE: {
        mCallback("", RtAudioDeviceParam::DEVICE_REMOVED);
    } break;
    default:
        break;
    }
}
