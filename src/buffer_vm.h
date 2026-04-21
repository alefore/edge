#include <memory>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/language/gc.h"
#include "src/language/gc_expanders.h"
#include "src/language/gc_view.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/callbacks_gc.h"
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
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, language::gc::Root<editor::OpenBuffer> value);
  static const types::ObjectName object_type_name;
};

template <>
struct VMTypeMapper<language::NonNull<std::shared_ptr<concurrent::Protected<
    std::vector<language::gc::Ptr<editor::OpenBuffer>>>>>> {
  static language::NonNull<std::shared_ptr<concurrent::Protected<
      std::vector<language::gc::Ptr<editor::OpenBuffer>>>>>
  get(Value& value);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      language::NonNull<std::shared_ptr<concurrent::Protected<
          std::vector<language::gc::Root<editor::OpenBuffer>>>>>
          input);
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      language::NonNull<std::shared_ptr<concurrent::Protected<
          std::vector<language::gc::Ptr<editor::OpenBuffer>>>>>
          input);
  static const types::ObjectName object_type_name;
};

template <>
struct VMTypeMapper<language::NonNull<std::shared_ptr<concurrent::Protected<
    std::vector<language::gc::Root<editor::OpenBuffer>>>>>> {
  static language::gc::Root<Value> New(
      language::gc::Pool& pool,
      language::NonNull<std::shared_ptr<concurrent::Protected<
          std::vector<language::gc::Root<editor::OpenBuffer>>>>>
          input);
  static const types::ObjectName object_type_name;
};

}  // namespace afc::vm
