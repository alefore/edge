#ifndef __AFC_EDITOR_SRC_INFRASTRUCTURE_SCREEN_LINE_MODIFIER_VM_H__
#define __AFC_EDITOR_SRC_INFRASTRUCTURE_SCREEN_LINE_MODIFIER_VM_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/language/error/value_or_error.h"
#include "src/language/safe_types.h"
#include "src/transformation_type.h"
#include "src/vm/callbacks.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
template <>
struct VMTypeMapper<afc::infrastructure::screen::LineModifier> {
  static std::expected<afc::infrastructure::screen::LineModifier,
                       afc::language::Error>
  get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      afc::infrastructure::screen::LineModifier value);
  static const types::ObjectName object_type_name;
};

class Environment;
template <>
const types::ObjectName VMTypeMapper<
    language::NonNull<std::shared_ptr<concurrent::Protected<std::set<
        afc::infrastructure::screen::LineModifier>>>>>::object_type_name;

}  // namespace afc::vm
namespace afc::infrastructure::screen {
void RegisterLineModifer(language::gc::Pool& pool,
                         vm::Environment& environment);
}  // namespace afc::infrastructure::screen

#endif  // __AFC_EDITOR_SRC_INFRASTRUCTURE_SCREEN_LINE_MODIFIER_VM_H__
