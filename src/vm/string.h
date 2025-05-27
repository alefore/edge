#ifndef __AFC_VM_INTERNAL_STRING_H__
#define __AFC_VM_INTERNAL_STRING_H__

#include <memory>

#include "src/concurrent/protected.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
class Environment;
void RegisterStringType(language::gc::Pool& pool, Environment& environment);

template <>
const types::ObjectName
    VMTypeMapper<language::NonNull<std::shared_ptr<concurrent::Protected<
        std::vector<language::lazy_string::LazyString>>>>>::object_type_name;

}  // namespace afc::vm

#endif  // __AFC_VM_INTERNAL_STRING_H__
