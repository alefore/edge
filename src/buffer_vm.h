#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/value.h"

namespace afc::vm {
class ObjectType;
class Environment;
}  // namespace afc::vm
namespace afc::editor {
class OpenBuffer;
void DefineBufferType(language::gc::Pool& pool, vm::Environment& environment);
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<language::gc::Root<editor::OpenBuffer>> {
  static language::gc::Root<editor::OpenBuffer> get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, language::gc::Root<editor::OpenBuffer> value);
  static const types::ObjectName object_type_name;
};

// TODO(2024-05-14): There could be leaks here. This should ideally keep only
// gc::Ptr. But that requires an expand function. We can probably do that by
// defining a VMTypeMapper for vectors of gc::Ptr<T>.
template <>
const types::ObjectName VMTypeMapper<language::NonNull<std::shared_ptr<
    std::vector<language::gc::Root<editor::OpenBuffer>>>>>::object_type_name;
}  // namespace afc::vm
