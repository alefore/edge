#include "src/infrastructure/screen/line_modifier_vm.h"

#include <glog/logging.h>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/single_line.h"
#include "src/vm/callbacks.h"
#include "src/vm/container.h"
#include "src/vm/environment.h"
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::infrastructure::screen::ColorToString;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::NonNull;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::vm::Environment;

namespace afc {
namespace vm {
template <>
const types::ObjectName
    VMTypeMapper<infrastructure::screen::Color>::object_type_name =
        types::ObjectName{IDENTIFIER_CONSTANT(L"Color")};

template <>
const types::ObjectName
    VMTypeMapper<infrastructure::screen::StyleAttribute>::object_type_name =
        types::ObjectName{IDENTIFIER_CONSTANT(L"StyleAttribute")};

template <>
const types::ObjectName VMTypeMapper<Style>::object_type_name =
    types::ObjectName{IDENTIFIER_CONSTANT(L"Style")};

/* static */ Style VMTypeMapper<Style>::get(Value& value) {
  return value.get_user_value<Style>(object_type_name).value();
}

/* static */ language::gc::Root<Value>
VMTypeMapper<infrastructure::screen::Color>::New(
    language::gc::Pool& pool, infrastructure::screen::Color value) {
  return Value::NewString(pool, ToLazyString(ColorToString(value)));
}

/* static */ language::gc::Root<Value> VMTypeMapper<Style>::New(
    language::gc::Pool& pool, Style value) {
  return Value::NewObject(pool, object_type_name,
                          MakeNonNullShared<Style>(value));
}
}  // namespace vm
namespace infrastructure::screen {
void RegisterLineModifier(gc::Pool&, Environment&) {}
}  // namespace infrastructure::screen
}  // namespace afc
