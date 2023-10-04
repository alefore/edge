#ifndef __AFC_VM_DEFAULT_ENVIRONMENT__
#define __AFC_VM_DEFAULT_ENVIRONMENT__

#include "src/language/gc.h"
#include "src/vm/environment.h"

namespace afc::vm {
language::gc::Root<Environment> NewDefaultEnvironment(language::gc::Pool& pool);
}
#endif
