#include "RtApiPulseSystemCallback.h"
#include "PulseCommon.h"
#include <pulse/context.h>
#include <pulse/def.h>
#include <pulse/mainloop.h>
#include <pulse/pulseaudio.h>

namespace {

void rt_pa_sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *userdata)
{
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    if (eol)
        return;
    printf("Output: ");
    opaque->callback(i->name, RtAudioDeviceParam::DEFAULT_CHANGED);
}

void rt_pa_source_info_cb(pa_context *c, const pa_source_info *i, int eol, void *userdata)
{
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    if (eol)
        return;
    printf("Input: ");
    opaque->callback(i->name, RtAudioDeviceParam::DEFAULT_CHANGED);
}

void rt_pa_card_info_added_cb(pa_context *c, const pa_card_info *i, int eol, void *userdata)
{
    if (eol)
        return;
    RtApiPulseSystemCallback::PaEventsUserData *opaque
        = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
    opaque->callback(i->name, RtAudioDeviceParam::DEVICE_ADDED);
}

void checkDefaultChanged(pa_context *c, uint32_t idx, unsigned int t, void *userdata)
{
    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    unsigned int evt = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    switch (evt) {
    case PA_SUBSCRIPTION_EVENT_CHANGE: {
        switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            pa_context_get_sink_info_by_index(c, idx, &rt_pa_sink_info_cb, userdata);
            break;
        case PA_SUBSCRIPTION_EVENT_SOURCE:
            pa_context_get_source_info_by_index(c, idx, &rt_pa_source_info_cb, userdata);
            break;
        default:
            break;
        }
    } break;
    default:
        break;
    }
}

void checkCardAddedRemoved(unsigned int t,
                           pa_context *c,
                           uint32_t idx,
                           RtApiPulseSystemCallback::PaEventsUserData *userdata)
{
    unsigned int evt = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    switch (evt) {
    case PA_SUBSCRIPTION_EVENT_NEW:
        pa_context_get_card_info_by_index(c, idx, &rt_pa_card_info_added_cb, userdata);
        break;
    case PA_SUBSCRIPTION_EVENT_REMOVE:
        userdata->callback("", RtAudioDeviceParam::DEVICE_REMOVED);
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
    auto *opaque = reinterpret_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);

    unsigned facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    switch (facility) {
    case PA_SUBSCRIPTION_EVENT_SINK:
    case PA_SUBSCRIPTION_EVENT_SOURCE:
        checkDefaultChanged(c, idx, t, userdata);
        break;
    case PA_SUBSCRIPTION_EVENT_CARD: {
        checkCardAddedRemoved(t, c, idx, opaque);
    } break;
    default:
        break;
    }
}

void rt_pa_context_state_callback(pa_context *context, void *userdata)
{
    auto *paProbeInfo = static_cast<RtApiPulseSystemCallback::PaEventsUserData *>(userdata);
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
    mUserData.finished(0);
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

    /*PaContext context(pa_mainloop_get_api(ml.handle()));
    if (context.isValid() == false) {
        errorStream_ << "RtApiPulse::probeDevices: pa_context_new() failed.";
        error(RTAUDIO_WARNING, errorStream_.str());
        return;
    }*/
    //init(ml.handle(), context.handle());
    return;
}

bool RtApiPulseSystemCallback::init(pa_mainloop *ml, pa_context *context)
{
    int ret = 1;
    mUserData.callback = mCallback;
    mUserData.setMainloop(ml);
    if (mUserData.isValid() == false) {
        error(RTAUDIO_WARNING, "RtApiPulseSystemCallback::init: user data not valid.");
        return false;
    }

    mMainloopRunning = std::make_unique<PaMainloopRunning>(ml,
                                                           context,
                                                           rt_pa_context_state_callback,
                                                           &mUserData);

    if (mMainloopRunning->run() != RTAUDIO_NO_ERROR) {
        error(RTAUDIO_WARNING, "RtApiPulse::probeDevices: could not get server info.");
        return false;
    }
    return true;
}
