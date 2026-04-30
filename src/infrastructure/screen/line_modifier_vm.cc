#include "src/infrastructure/screen/line_modifier_vm.h"

#include <glog/logging.h>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/single_line.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::infrastructure::screen::ModifierFromString;
using afc::infrastructure::screen::ModifierToString;
using afc::language::Error;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::vm::Environment;

namespace afc {
namespace vm {
template <>
const types::ObjectName
    VMTypeMapper<infrastructure::screen::LineModifier>::object_type_name =
        types::ObjectName{
            Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"LineModifier")}};

/* static */ std::expected<infrastructure::screen::LineModifier, Error>
VMTypeMapper<infrastructure::screen::LineModifier>::get(Value& value) {
  return NonEmptySingleLine::New(SingleLine::New(value.get_string()))
      .and_then([](NonEmptySingleLine value_str) {
        return ModifierFromString(std::move(value_str));
      });
}

/* static */ language::gc::Root<Value>
VMTypeMapper<infrastructure::screen::LineModifier>::New(
    language::gc::Pool& pool, infrastructure::screen::LineModifier value) {
  return Value::NewString(pool, ToLazyString(ModifierToString(value)));
}
}  // namespace vm
namespace infrastructure::screen {
void RegisterLineModifier(gc::Pool& pool, Environment& environment) {
  // TODO(P2, 2026-04-30): Implement Setinfrastructure::screen::LineModifier.
}
}  // namespace infrastructure::screen
}  // namespace afc
