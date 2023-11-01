
class PROPVARIANT_Raii {
public:
    PROPVARIANT_Raii() {
        PropVariantInit(&mPropVal);
    }
    ~PROPVARIANT_Raii() {
        PropVariantClear(&mPropVal);
    }
    PROPVARIANT* operator&() {
        return &mPropVal;
    }
    const PROPVARIANT get() const {
        return mPropVal;
    }
private:
    PROPVARIANT mPropVal;
};
