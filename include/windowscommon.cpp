#include "windowscommon.h"



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
