// Attrappe: custattrib.h
#pragma once
#include "max.h"
class CustAttrib : public Animatable {
public:
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    virtual const MCHAR* GetName(bool localized) { (void)localized; return L"stub"; }
#else
    virtual const MCHAR* GetName() { return L"stub"; }
#endif
};
