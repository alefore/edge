#include "src/buffer_registry.h"

#include "src/language/gc_view.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"

namespace gc = afc::language::gc;

using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::VisitOptional;

namespace afc::editor {
void BufferRegistry::SetInitialCommands(gc::Ptr<OpenBuffer> buffer) {
  CHECK(initial_commands_ == std::nullopt);
  initial_commands_ = std::move(buffer);
}

gc::Ptr<OpenBuffer> BufferRegistry::MaybeAddFile(
    infrastructure::Path path,
    OnceOnlyFunction<gc::Root<OpenBuffer>()> factory) {
  // TODO(trivial, 2024-05-30): Only traverse the map once.
  if (auto it = files_.find(path); it != files_.end()) return it->second;
  return files_.insert({path, std::move(factory)().ptr()}).first->second;
}

void BufferRegistry::AddAnonymous(language::gc::Ptr<OpenBuffer> buffer) {
  anonymous_.push_back(buffer);
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
BufferRegistry::Expand() const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) {
        output.push_back(buffer.object_metadata());
      },
      [] {}, initial_commands_);
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) {
        output.push_back(buffer.object_metadata());
      },
      [] {}, paste_);
  for (NonNull<std::shared_ptr<gc::ObjectMetadata>> data :
       files_ | std::views::values | gc::view::ObjectMetadata)
    output.push_back(data);
  for (NonNull<std::shared_ptr<gc::ObjectMetadata>> data :
       anonymous_ | gc::view::ObjectMetadata)
    output.push_back(data);
  return output;
}

void BufferRegistry::SetPaste(gc::Ptr<OpenBuffer> buffer) {
  paste_ = std::move(buffer);
}

std::optional<gc::Ptr<OpenBuffer>> BufferRegistry::paste() const {
  return paste_;
}

}  // namespace afc::editor
