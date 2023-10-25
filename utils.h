#pragma once

#include <string>

std::string convertCharPointerToStdString(const wchar_t* text);
std::string convertCharPointerToStdString(const char* text);
std::wstring convertStdStringToWString(const std::string& text);

#if defined(_MSC_VER)
#define MUTEX_INITIALIZE(A) InitializeCriticalSection(A)
#define MUTEX_DESTROY(A)    DeleteCriticalSection(A)
#define MUTEX_LOCK(A)       EnterCriticalSection(A)
#define MUTEX_UNLOCK(A)     LeaveCriticalSection(A)
#else
#define MUTEX_INITIALIZE(A) pthread_mutex_init(A, NULL)
#define MUTEX_DESTROY(A)    pthread_mutex_destroy(A)
#define MUTEX_LOCK(A)       pthread_mutex_lock(A)
#define MUTEX_UNLOCK(A)     pthread_mutex_unlock(A)
#endif

#define SAFE_RELEASE( objectPtr )\
if ( objectPtr )\
{\
  objectPtr->Release();\
  objectPtr = NULL;\
}

template<class A>
class MutexRaii{
public:
    MutexRaii(A& a) : mMutex(a){
        MUTEX_LOCK(&mMutex);
    }
    ~MutexRaii(){
        MUTEX_UNLOCK(&mMutex);
    }
    MutexRaii(const MutexRaii&) = delete;
    MutexRaii& operator=(const MutexRaii&) = delete;
private:
    A& mMutex;
};
