#include "src/buffer_registry.h"

#include "src/language/safe_types.h"

namespace gc = afc::language::gc;

using afc::language::NonNull;
using afc::language::VisitOptional;

namespace afc::editor {
void BufferRegistry::SetInitialCommands(language::gc::Ptr<OpenBuffer> buffer) {
  CHECK(initial_commands_ == std::nullopt);
  initial_commands_ = std::move(buffer);
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
BufferRegistry::Expand() const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) {
        output.push_back(buffer.object_metadata());
      },
      [] {}, initial_commands_);
  return output;
}

}  // namespace afc::editor
