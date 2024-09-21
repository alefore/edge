#include "src/infrastructure/extended_char_vm.h"

#include <vector>

#include "src/concurrent/protected.h"
#include "src/infrastructure/extended_char.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/callbacks.h"
#include "src/vm/container.h"
#include "src/vm/types.h"

using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;

namespace afc::vm {
const types::ObjectName
    VMTypeMapper<infrastructure::ExtendedChar>::object_type_name =
        types::ObjectName{
            Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"ExtendedChar")}};

language::gc::Root<Value> VMTypeMapper<infrastructure::ExtendedChar>::New(
    language::gc::Pool& pool, infrastructure::ExtendedChar value) {
  return Value::NewObject(
      pool, object_type_name,
      MakeNonNullShared<infrastructure::ExtendedChar>(value));
}

afc::infrastructure::ExtendedChar
VMTypeMapper<infrastructure::ExtendedChar>::get(Value& value) {
  return value.get_user_value<infrastructure::ExtendedChar>(object_type_name)
      .value();
}

template <>
const types::ObjectName
    VMTypeMapper<NonNull<std::shared_ptr<concurrent::Protected<
        std::vector<infrastructure::ExtendedChar>>>>>::object_type_name =
        types::ObjectName{
            Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"VectorExtendedChar")}};

}  // namespace afc::vm
namespace afc::infrastructure {
void RegisterVectorExtendedChar(language::gc::Pool& pool,
                                vm::Environment& environment) {
  vm::container::Export<std::vector<ExtendedChar>>(pool, environment);
}
}  // namespace afc::infrastructure
