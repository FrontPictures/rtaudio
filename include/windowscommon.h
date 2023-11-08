#pragma once

#include <wrl.h>
#include <mmdeviceapi.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#define SMART_PTR_WRAPPER(ptr_type, datatype, remove) ptr_type<datatype, remove>
#define UNIQUE_WRAPPER(type, remove) SMART_PTR_WRAPPER(std::unique_ptr, type, remove)

#define UNIQUE_FORMAT UNIQUE_WRAPPER(WAVEFORMATEX, decltype(&CoTaskMemFree))
#define MAKE_UNIQUE_FORMAT_EMPTY UNIQUE_FORMAT(nullptr, CoTaskMemFree);

#define UNIQUE_STRING UNIQUE_WRAPPER(WCHAR, decltype(&CoTaskMemFree))
#define MAKE_UNIQUE_STRING_EMPTY UNIQUE_STRING(nullptr, CoTaskMemFree);

#define UNIQUE_EVENT UNIQUE_WRAPPER(void, decltype(&CloseHandle))
#define MAKE_UNIQUE_EVENT_VALUE(v) UNIQUE_EVENT(v, CloseHandle)
#define MAKE_UNIQUE_EVENT_EMPTY MAKE_UNIQUE_EVENT_VALUE(nullptr)


#define CONSTRUCT_UNIQUE_FORMAT(create, out_res) makeUniqueContructed<HRESULT, WAVEFORMATEX, decltype(&CoTaskMemFree)>([&](WAVEFORMATEX** ptr) {return create(ptr);}, out_res, CoTaskMemFree)
#define CONSTRUCT_UNIQUE_STRING(create, out_res) makeUniqueContructed<HRESULT, WCHAR, decltype(&CoTaskMemFree)>([&](LPWSTR* ptr) {return create(ptr);}, out_res, CoTaskMemFree)

template<class Result, class Type, class Remove>
inline Result makeUniqueContructed(std::function<Result(Type**)> cc,
    std::unique_ptr<Type, Remove>& out_result, Remove remove_fun) {
    Type* temp = nullptr;
    Result res = cc(&temp);
    out_result = std::move(UNIQUE_WRAPPER(Type, Remove)(temp, remove_fun));
    return res;
}

class PROPVARIANT_Raii {
public:
    PROPVARIANT_Raii();
    ~PROPVARIANT_Raii();
    PROPVARIANT* operator&();
    const PROPVARIANT get() const;
private:
    PROPVARIANT mPropVal;
};
