// Attrappe: ICustAttribContainer.h
#pragma once
#include "custattrib.h"
class ICustAttribContainer {
public:
    virtual ~ICustAttribContainer() {}
    virtual int GetNumCustAttribs() { return 0; }
    virtual CustAttrib* GetCustAttrib(int) { return nullptr; }
};
