#ifndef __AFC_EDITOR_SRC_INFRASTRUCTURE_SCREEN_LINE_MODIFIER_VM_H__
#define __AFC_EDITOR_SRC_INFRASTRUCTURE_SCREEN_LINE_MODIFIER_VM_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
template <>
struct VMTypeMapper<afc::infrastructure::screen::Color> {
  static std::expected<afc::infrastructure::screen::Color, afc::language::Error>
  get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, afc::infrastructure::screen::Color value);
  static const types::ObjectName object_type_name;
};

template <>
struct VMTypeMapper<afc::infrastructure::screen::StyleAttribute> {
  static std::expected<afc::infrastructure::screen::StyleAttribute,
                       afc::language::Error>
  get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      afc::infrastructure::screen::StyleAttribute value);
  static const types::ObjectName object_type_name;
};

// TODO(2026-05-06, P2, trivial): Remove the wrapping. Instead, make Style
// deeply immutable.
template <>
struct VMTypeMapper<afc::infrastructure::screen::Style> {
  static afc::infrastructure::screen::Style get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, afc::infrastructure::screen::Style value);
  static const types::ObjectName object_type_name;
};
class Environment;
}  // namespace afc::vm
namespace afc::infrastructure::screen {
void RegisterLineModifier(language::gc::Pool& pool,
                          vm::Environment& environment);
}  // namespace afc::infrastructure::screen

#endif  // __AFC_EDITOR_SRC_INFRASTRUCTURE_SCREEN_LINE_MODIFIER_VM_H__
