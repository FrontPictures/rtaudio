#include "ThreadSuspendable.h"
#include <cassert>

ThreadSuspendable::ThreadSuspendable(std::function<bool()> process)
    : mProcess(process)
{
    mThread = std::thread(&ThreadSuspendable::threadMethod, this);
}

ThreadSuspendable::~ThreadSuspendable()
{
    if (mThread.joinable()) {
        stop(true);
        mThread.join();
    }
}

bool ThreadSuspendable::resume(bool blockable)
{
    std::unique_lock g(mMutex);
    if (mState != State::SUSPENDED)
        return false;
    mState = State::RESUMING;
    mCV.notify_one();
    if (blockable) {
        mCV.wait(g);
    }
    return true;
}

bool ThreadSuspendable::suspend(bool blockable)
{
    std::unique_lock g(mMutex);
    if (mState != State::RUNNING)
        return false;
    mState = State::SUSPENDING;
    mCV.notify_one();
    if (blockable) {
        mCV.wait(g);
    }
    return true;
}

bool ThreadSuspendable::stop(bool blockable)
{
    std::unique_lock g(mMutex);
    if (mState != State::SUSPENDED && mState != State::RUNNING)
        return false;
    mState = State::STOPPING;
    mCV.notify_one();
    if (blockable) {
        mCV.wait(g);
    }
    return true;
}

bool ThreadSuspendable::isValid() const
{
    return mThread.joinable();
}

void ThreadSuspendable::threadMethod()
{
    bool lastProcessResult = true;
    while (true) {
        std::unique_lock g(mMutex);
        if (lastProcessResult == false) {
            mState = State::STOPPING;
        }

        while (mState != State::RUNNING) {
            switch (mState) {
            case State::SUSPENDED:
                mCV.wait(g);
                break;
            case State::RESUMING:
                mState = State::RUNNING;
                mCV.notify_one();
                break;
            case State::SUSPENDING:
                mState = State::SUSPENDED;
                mCV.notify_one();
            case State::STOPPING:
                mState = State::STOPPED;
                mCV.notify_one();
                return;
            case State::RUNNING:
            case State::STOPPED:
            default:
                assert(false);
                return;
            }
        }
        lastProcessResult = mProcess();
    }
}
