#include "PaMainloop.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <pulse/mainloop.h>
#include <pulse/operation.h>

PaMainloop::PaMainloop()
{
    mMainloop = pa_mainloop_new();
}

bool PaMainloop::runUntil(std::function<bool()> postdicate)
{
    if (!isValid())
        return false;
    do {
        int retVal = 0;
        if (pa_mainloop_iterate(mMainloop, 1, &retVal) < 0) {
            mErrorWhileRunning = true;
            return false;
        }
        processAsyncTasks();
    } while (postdicate() == false);
    return true;
}

bool PaMainloop::iterateBlocking()
{
    if (!isValid())
        return false;
    int retVal = 0;
    if (pa_mainloop_iterate(mMainloop, 1, &retVal) < 0) {
        mErrorWhileRunning = true;
        return false;
    }
    processAsyncTasks();
    return true;
}

bool PaMainloop::stop()
{
    if (!isValid())
        return false;
    pa_mainloop_quit(mMainloop, 0);
    return true;
}

bool PaMainloop::addTask(std::shared_ptr<PaMainloopTask> task)
{
    if (!isValid())
        return false;
    mTasks.push_back(std::move(task));
    return true;
}

void PaMainloop::processAsyncTasks()
{
    for (auto it = mTasks.begin(); it != mTasks.end();) {
        if ((*it)->process()) {
            it = mTasks.erase(it);
        } else {
            it++;
        }
    }
}

void PaMainloop::cancelAllTasks()
{
    for (auto it = mTasks.begin(); it != mTasks.end();) {
        (*it)->cancel();
    }
}

PaMainloop::~PaMainloop()
{
    if (!isValid())
        return;
    stop();
    cancelAllTasks();
    if (mErrorWhileRunning == false) {
        pa_mainloop_run(mMainloop, nullptr);
    }
    pa_mainloop_free(mMainloop);
    mMainloop = nullptr;
}

bool PaMainloop::isValid() const
{
    return mMainloop ? true : false;
}

pa_mainloop *PaMainloop::handle() const
{
    return mMainloop;
}

PaMainloopTask::PaMainloopTask(pa_operation *oper,
                               std::shared_ptr<OpaqueResultError> opaq,
                               std::function<void(std::shared_ptr<OpaqueResultError>)> resClb)
    : mOperation(oper)
    , mOpaque(opaq)
    , mResultCallback(resClb)
{
    assert(oper && opaq && resClb);
}

PaMainloopTask::~PaMainloopTask()
{
    auto state = pa_operation_get_state(mOperation);
    assert(state != PA_OPERATION_RUNNING);
    pa_operation_unref(mOperation);
}

bool PaMainloopTask::process()
{
    auto state = pa_operation_get_state(mOperation);
    if (state == PA_OPERATION_RUNNING) {
        return false;
    }
    if (state == PA_OPERATION_DONE) {
        mResultCallback(std::move(mOpaque));
    }
    return true;
}

void PaMainloopTask::cancel()
{
    pa_operation_cancel(mOperation);
}
