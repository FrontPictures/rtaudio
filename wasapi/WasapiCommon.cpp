#include "WasapiCommon.h"

RtApiWasapiCommon::RtApiWasapiCommon()
{
    HRESULT hr = CoInitialize(NULL);
    if (!FAILED(hr))
        coInitialized_ = true;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator_);
}

RtApiWasapiCommon::~RtApiWasapiCommon()
{
    deviceEnumerator_.Reset();
    if (coInitialized_)
        CoUninitialize();
}

PROPVARIANT_Raii::PROPVARIANT_Raii() {
    PropVariantInit(&mPropVal);
}

PROPVARIANT_Raii::~PROPVARIANT_Raii() {
    PropVariantClear(&mPropVal);
}

PROPVARIANT* PROPVARIANT_Raii::operator&() {
    return &mPropVal;
}

const PROPVARIANT PROPVARIANT_Raii::get() const {
    return mPropVal;
}
