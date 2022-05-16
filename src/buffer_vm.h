#include <memory>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/value.h"

namespace afc::vm {
class ObjectType;
}
namespace afc::language::gc {
class Pool;
}
namespace afc::editor {
class OpenBuffer;
language::NonNull<std::unique_ptr<vm::ObjectType>> BuildBufferType(
    language::gc::Pool& pool);
}  // namespace afc::editor
namespace afc::vm {
template <>
struct VMTypeMapper<language::gc::Root<editor::OpenBuffer>> {
  static language::gc::Root<editor::OpenBuffer> get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, language::gc::Root<editor::OpenBuffer> value);
  static const VMType vmtype;
};
}  // namespace afc::vm
