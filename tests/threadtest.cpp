#include "ThreadSuspendable.h"
#include <cassert>
#include <chrono>
#include <cstdio>

int index = 5;

bool task(int *index)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    printf("Task %d\n", *index);
    (*index)++;
    if (*index > 15) {
        return false;
    }
    return true;
}

int main()
{
    int index = 0;
    int *index_ptr = &index;
    ThreadSuspendable thread([index_ptr]() { return task(index_ptr); });
    assert(thread.isValid());
    printf("Thread created\n");
    printf("Thread resuming\n");
    thread.resume(true);
    printf("Thread resumed\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    printf("Thread suspending\n");
    thread.suspend();
    printf("Thread suspended\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    printf("Thread resuming\n");
    thread.resume(true);
    printf("Thread resumed\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    printf("Thread suspending\n");
    thread.suspend();
    printf("Thread suspended\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    printf("Thread stopping\n");
    thread.stop();
    printf("Thread stopped\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    return 0;
}
