#include "ThreadSuspendable.h"
#include <cassert>

namespace {
inline int clampPriority(int priority, int policy)
{
    int min = sched_get_priority_min(policy);
    int max = sched_get_priority_max(policy);
    priority = std::max(priority, min);
    priority = std::min(priority, max);
    return priority;
}

static void *threadMethodJump(void *user)
{
    ThreadSuspendable *c = static_cast<ThreadSuspendable *>(user);
    c->threadMethod();
    pthread_exit(NULL);
    return nullptr;
}

pthread_attr_t setupAttrsPriority(int priority)
{
    pthread_attr_t attr{};
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

#ifdef SCHED_RR // Undefined with some OSes (e.g. NetBSD 1.6.x with GNU Pthread)
    struct sched_param param
    {};
    priority = clampPriority(priority, SCHED_RR);
    param.sched_priority = priority;

    // Set the policy BEFORE the priority. Otherwise it fails.
    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    // This is definitely required. Otherwise it fails.
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedparam(&attr, &param);
#else
    pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
#endif
    return attr;
}

} // namespace

ThreadSuspendable::ThreadSuspendable(std::function<bool()> process, bool realtime, int priority)
    : mProcess(process)
{
#ifdef WIN32
    mThread = std::thread(&ThreadSuspendable::threadMethod, this, realtime, priority);
#else
    pthread_attr_t attr = setupAttrsPriority(priority);
    int result = pthread_create(&mThread, &attr, threadMethodJump, this);
    pthread_attr_destroy(&attr);
    if (result != 0) {
        pthread_create(&mThread, nullptr, threadMethodJump, this);
    }
#endif
}

ThreadSuspendable::~ThreadSuspendable()
{
#ifdef WIN32
    if (mThread.joinable()) {
        stop(true);
        mThread.join();
    }
#endif
    if (mThread) {
        stop(true);
        pthread_join(mThread, 0);
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
#ifdef WIN32
    return mThread.joinable();
#else
    return mThread != 0;
#endif
}

void ThreadSuspendable::threadMethod()
{
    bool lastProcessResult = true;
    while (true) {
        {
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
                    break;
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
        }
        lastProcessResult = mProcess();
    }
}
