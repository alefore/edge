#ifndef __AFC_INFRASTRUCTURE_EXTENDED_CHAR_VM_H__
#define __AFC_INFRASTRUCTURE_EXTENDED_CHAR_VM_H__

#include "src/concurrent/protected.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"

using afc::concurrent::Protected;

namespace afc::infrastructure {
void RegisterVectorExtendedChar(language::gc::Pool&, vm::Environment&);
}
namespace afc::vm {
template <>
struct VMTypeMapper<afc::infrastructure::ExtendedChar> {
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       afc::infrastructure::ExtendedChar value);
  static afc::infrastructure::ExtendedChar get(Value& value);
  static const types::ObjectName object_type_name;
};

template <>
const types::ObjectName VMTypeMapper<language::NonNull<std::shared_ptr<
    Protected<std::vector<infrastructure::ExtendedChar>>>>>::object_type_name;
}  // namespace afc::vm
#endif  // __AFC_INFRASTRUCTURE_EXTENDED_CHAR_VM_H__
