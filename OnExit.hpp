#pragma once
#include <functional>

class OnExit
{
public:
    OnExit(std::function<void()> func) : mFunc(func) {}

    OnExit(const OnExit&) = delete;
    OnExit& operator=(const OnExit&) = delete;

    void invalidate() {
        mFunc = nullptr;
    }

    ~OnExit()
    {
        if (mFunc) {
            mFunc();
        }
    }

private:
    std::function<void()> mFunc;
};
