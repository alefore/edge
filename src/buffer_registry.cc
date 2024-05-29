#include "src/buffer_registry.h"

#include "src/language/safe_types.h"

namespace gc = afc::language::gc;

using afc::language::NonNull;
using afc::language::VisitOptional;

namespace afc::editor {
void BufferRegistry::SetInitialCommands(gc::Ptr<OpenBuffer> buffer) {
  CHECK(initial_commands_ == std::nullopt);
  initial_commands_ = std::move(buffer);
}

void BufferRegistry::AddFile(infrastructure::Path path,
                             gc::Ptr<OpenBuffer> buffer) {
  files_.insert({path, std::move(buffer)});
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

void BufferRegistry::SetPaste(gc::Ptr<OpenBuffer> buffer) {
  paste_ = std::move(buffer);
}

std::optional<gc::Ptr<OpenBuffer>> BufferRegistry::paste() const {
  return paste_;
}

}  // namespace afc::editor
