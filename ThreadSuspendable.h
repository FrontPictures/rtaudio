#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#ifdef WIN32
#include <thread>
#else
#include <pthread.h>
#endif

class ThreadSuspendable
{
public:
    ThreadSuspendable(std::function<bool()> process, bool realtime = false, int priority = 0);
    ThreadSuspendable(const ThreadSuspendable &) = delete;
    ThreadSuspendable &operator=(const ThreadSuspendable &) = delete;
    ~ThreadSuspendable();

    void resume();
    void suspend();
    void stop();
    bool isValid() const;

    //do not call this
    void threadMethod();

private:
    enum class State { SUSPENDED, RUNNING, STOPPED, RESUMING, SUSPENDING, STOPPING };
    State mState = State::SUSPENDED;
    std::function<bool()> mProcess = nullptr;
#ifdef WIN32
    std::thread mThread;
#else
    pthread_t mThread = 0;
#endif

    std::mutex mMutex;
    std::condition_variable mCV;
};
