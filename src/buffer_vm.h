#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/value.h"

namespace afc::vm {
class ObjectType;
}
namespace afc::editor {
class OpenBuffer;
language::gc::Root<vm::ObjectType> BuildBufferType(language::gc::Pool& pool);
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<language::gc::Root<editor::OpenBuffer>> {
  static language::gc::Root<editor::OpenBuffer> get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, language::gc::Root<editor::OpenBuffer> value);
  static const VMTypeObjectTypeName object_type_name;
};
}  // namespace afc::vm
