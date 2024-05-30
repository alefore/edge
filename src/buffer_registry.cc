#include "src/buffer_registry.h"

#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/once_only_function.h"
#include "src/language/safe_types.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::Path;
using afc::language::GetValueOrDefault;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::VisitOptional;

namespace afc::editor {
void BufferRegistry::SetInitialCommands(gc::Ptr<OpenBuffer> buffer) {
  CHECK(initial_commands_ == std::nullopt);
  initial_commands_ = std::move(buffer);
}

gc::Ptr<OpenBuffer> BufferRegistry::MaybeAdd(
    const BufferName& id, OnceOnlyFunction<gc::Root<OpenBuffer>()> factory) {
  // TODO(trivial, 2024-05-30): Only traverse the map once.
  if (auto it = buffer_map_.find(id); it != buffer_map_.end())
    return it->second;
  return buffer_map_.insert({id, std::move(factory)().ptr()}).first->second;
}

void BufferRegistry::Add(const BufferName& name, gc::Ptr<OpenBuffer> buffer) {
  // TODO(2024-05-30, trivial): Detect errors if a server already was there.
  buffer_map_.insert({name, std::move(buffer)});
}

std::optional<gc::Ptr<OpenBuffer>> BufferRegistry::Find(
    const BufferName& name) {
  if (auto it = buffer_map_.find(name); it != buffer_map_.end())
    return it->second;
  return std::nullopt;
}

void BufferRegistry::AddAnonymous(gc::Ptr<OpenBuffer> buffer) {
  anonymous_.push_back(buffer);
}

std::vector<gc::Ptr<OpenBuffer>> BufferRegistry::buffers() const {
  std::vector<gc::Ptr<OpenBuffer>> output;
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) { output.push_back(buffer); },
      [] {}, initial_commands_);
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) { output.push_back(buffer); },
      [] {}, paste_);
  auto buffer_map_values = buffer_map_ | std::views::values;
  output.insert(output.end(), buffer_map_values.begin(),
                buffer_map_values.end());

  output.insert(output.end(), anonymous_.begin(), anonymous_.end());
  return output;
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
BufferRegistry::Expand() const {
  return container::MaterializeVector(buffers() | gc::view::ObjectMetadata);
}

void BufferRegistry::SetPaste(gc::Ptr<OpenBuffer> buffer) {
  paste_ = std::move(buffer);
}

std::optional<gc::Ptr<OpenBuffer>> BufferRegistry::paste() const {
  return paste_;
}

}  // namespace afc::editor
