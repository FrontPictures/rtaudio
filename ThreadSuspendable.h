#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class ThreadSuspendable
{
public:
    ThreadSuspendable(std::function<bool()> process);
    ThreadSuspendable(const ThreadSuspendable &) = delete;
    ThreadSuspendable &operator=(const ThreadSuspendable &) = delete;
    ~ThreadSuspendable();

    bool resume(bool blockable = false);
    bool suspend(bool blockable = true);
    bool stop(bool blockable = true);
    bool isValid() const;

private:
    void threadMethod();
    enum class State { SUSPENDED, RUNNING, STOPPED, RESUMING, SUSPENDING, STOPPING };
    State mState = State::SUSPENDED;
    std::function<bool()> mProcess = nullptr;
    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCV;
};
