#include "RtApiPulseSystemCallback.h"
#include "PulseCommon.h"
#include <pulse/context.h>
#include <pulse/def.h>
#include <pulse/mainloop.h>
#include <pulse/pulseaudio.h>

namespace {

void pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    if (eol)
        return;
    printf("Output: ");
    opaque->callback(i->name, RtAudioDeviceParam::DEFAULT_CHANGED);
}

void pa_source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    if (eol)
        return;
    printf("Input: ");
    opaque->callback(i->name, RtAudioDeviceParam::DEFAULT_CHANGED);
}

void pa_card_info_cb_added(pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
    if (eol)
        return;
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    opaque->callback(i->name, RtAudioDeviceParam::DEVICE_ADDED);
}

void checkDefaultChanged(pa_context *c,
                         uint32_t idx,
                         RtAudioDeviceCallbackLambda callback,
                         unsigned int t,
                         void *userdata)
{
    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    unsigned int evt = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    switch (evt) {
    case PA_SUBSCRIPTION_EVENT_CHANGE: {
        switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            pa_context_get_sink_info_by_index(c, idx, &pa_sink_info_cb, userdata);
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE:
            pa_context_get_source_info_by_index(c, idx, &pa_source_info_cb, userdata);
            break;
        default:
            break;
        }
    } break;
    default:
        break;
    }
}

void checkCardAddedRemoved(RtAudioDeviceCallbackLambda callback,
                           unsigned int t,
                           pa_context *c,
                           uint32_t idx,
                           void *userdata)
{
    unsigned int evt = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    switch (evt) {
    case PA_SUBSCRIPTION_EVENT_NEW:
        pa_context_get_card_info_by_index(c, idx, &pa_card_info_cb_added, userdata);
        break;
    case PA_SUBSCRIPTION_EVENT_REMOVE:
        callback("", RtAudioDeviceParam::DEVICE_REMOVED);
        break;
    default:
        break;
    }
}

void pa_context_subscribe_cb(pa_context *c,
                             pa_subscription_event_type_t t,
                             uint32_t idx,
                             void *userdata)
{
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);

    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    switch (facility) {
    case PA_SUBSCRIPTION_EVENT_SINK:
    case PA_SUBSCRIPTION_EVENT_SOURCE:
        checkDefaultChanged(c, idx, opaque->callback, t, userdata);
        break;
    case PA_SUBSCRIPTION_EVENT_CARD: {
        checkCardAddedRemoved(opaque->callback, t, c, idx, userdata);
    } break;
    default:
        break;
    }
}

void rt_pa_context_state_callback(pa_context *context, void *userdata)
{
    RtApiPulseSystemCallback::PaEventsUserData *paProbeInfo
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    auto state = pa_context_get_state(context);
    pa_operation *operation = nullptr;
    switch (state) {
    case PA_CONTEXT_READY:
        pa_context_set_subscribe_callback(context, pa_context_subscribe_cb, userdata);
        operation = pa_context_subscribe(context,
                                         static_cast<pa_subscription_mask_t>(
                                             PA_SUBSCRIPTION_MASK_ALL),
                                         nullptr,
                                         nullptr);
        if (operation) {
            pa_operation_unref(operation);
        }
        break;

    case PA_CONTEXT_TERMINATED:
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 0);
        break;
    case PA_CONTEXT_FAILED:
        paProbeInfo->paMainLoopApi->quit(paProbeInfo->paMainLoopApi, 1);
        break;
    default:
        break;
    }
}

} // namespace

RtApiPulseSystemCallback::RtApiPulseSystemCallback(RtAudioDeviceCallbackLambda callback)
    : mCallback(callback)
{
    mNotificationThread = std::thread(&RtApiPulseSystemCallback::notificationThread, this);
}

RtApiPulseSystemCallback::~RtApiPulseSystemCallback()
{
    mUserData.paMainLoopApi->quit(mUserData.paMainLoopApi, 0);
    mNotificationThread.join();
}

void RtApiPulseSystemCallback::notificationThread()
{
    PaMainloop ml;
    if (ml.isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_new() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return;
    }

    PaContext context(pa_mainloop_get_api(ml.handle()));
    if (context.isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return;
    }
    initWithHandles(ml.handle(), context.handle());
}

bool RtApiPulseSystemCallback::initWithHandles(pa_mainloop *ml, pa_context *context)
{
    int ret = 1;
    mUserData.callback = mCallback;
    mUserData.paMainLoopApi = pa_mainloop_get_api(ml);

    pa_context_set_state_callback(context, rt_pa_context_state_callback, &mUserData);

    if (pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_connect() failed: "
                     << pa_strerror(pa_context_errno(context));
        error(RTAUDIO_WARNING, errorStream_.str());
        return false;
    }

    if (pa_mainloop_run(ml, &ret) < 0) {
        errorStream_ << "RtApiPulse::probeDevices: pa_mainloop_run() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return false;
    }

    if (ret != 0) {
        errorStream_ << "RtApiPulse::probeDevices: could not get server info.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return false;
    }
    return true;
}
