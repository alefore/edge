#include <memory>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/language/gc.h"
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

// TODO(2025-05-27, trivial): This doesn't really belong here. Maybe it should
// go to src/language/gc.h or similar.
template <typename V>
std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
Expand(const language::NonNull<std::shared_ptr<
           concurrent::Protected<std::vector<language::gc::Ptr<V>>>>>& input) {
  return input->lock([](const std::vector<language::gc::Ptr<V>>& contents) {
    return language::container::MaterializeVector(
        contents | language::gc::view::ObjectMetadata);
  });
}

}  // namespace afc::vm
