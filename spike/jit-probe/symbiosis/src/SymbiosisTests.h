#ifndef SymbiosisTests_h
#define SymbiosisTests_h

#include "SymbiosisABI.h"

namespace symbiosis {

bool runSelfTest();
const UTF8Z* getLastTestFailure();

} // namespace symbiosis

#endif // SymbiosisTests_h
