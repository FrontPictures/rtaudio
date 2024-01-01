#pragma once
#include <functional>
#include <list>
#include <memory>

struct pa_mainloop;
struct pa_operation;
class OpaqueResultError;

class PaMainloopTask
{
public:
    PaMainloopTask(pa_operation *oper,
                   std::shared_ptr<OpaqueResultError> opaq,
                   std::function<void(std::shared_ptr<OpaqueResultError>)> resClb);
    ~PaMainloopTask();
    bool process();
    void cancel();

private:
    pa_operation *mOperation = nullptr;
    std::shared_ptr<OpaqueResultError> mOpaque;
    std::function<void(std::shared_ptr<OpaqueResultError>)> mResultCallback;
};

class PaMainloop
{
public:
    PaMainloop();
    ~PaMainloop();
    bool isValid() const;
    pa_mainloop *handle() const;

    PaMainloop(const PaMainloop &) = delete;
    PaMainloop &operator=(const PaMainloop &) = delete;

    bool runUntil(std::function<bool()>);
    bool iterateBlocking();
    bool stop();
    bool addTask(std::shared_ptr<PaMainloopTask> task);

private:
    void processAsyncTasks();
    void cancelAllTasks();

    bool mErrorWhileRunning = false;
    pa_mainloop *mMainloop = NULL;
    std::list<std::shared_ptr<PaMainloopTask>> mTasks;
};
