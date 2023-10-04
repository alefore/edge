#ifndef __AFC_INFRASTRUCTURE_DIRNAME_VM_H__
#define __AFC_INFRASTRUCTURE_DIRNAME_VM_H__

#include <memory>

#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/value.h"

namespace afc::vm {
template <>
struct VMTypeMapper<infrastructure::Path> {
  static language::ValueOrError<infrastructure::Path> get(Value& value);
  static language::gc::Root<vm::Value> New(language::gc::Pool& pool,
                                           infrastructure::Path value);
  static const Type vmtype;
};
}  // namespace afc::vm
#endif  // __AFC_INFRASTRUCTURE_DIRNAME_VM_H__
