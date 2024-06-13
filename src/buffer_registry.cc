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
gc::Root<OpenBuffer> BufferRegistry::MaybeAdd(
    const BufferName& id, OnceOnlyFunction<gc::Root<OpenBuffer>()> factory) {
  // TODO(trivial, 2024-05-30): Only traverse the map once.
  if (auto it = buffer_map_.find(id); it != buffer_map_.end())
    if (std::optional<gc::Root<OpenBuffer>> previous_buffer = it->second.Lock();
        previous_buffer.has_value())
      return previous_buffer.value();

  gc::Root<OpenBuffer> buffer = std::move(factory)();
  Add(id, buffer.ptr().ToWeakPtr());
  return buffer;
}

void BufferRegistry::Add(const BufferName& name,
                         gc::WeakPtr<OpenBuffer> buffer) {
  // TODO(2024-05-30, trivial): Detect errors if a server already was there.
  if (std::holds_alternative<FuturePasteBuffer>(name) ||
      std::holds_alternative<HistoryBufferName>(name))
    retained_buffers_.insert({name, buffer.Lock()->ptr()});
  buffer_map_.insert({name, std::move(buffer)});
}

std::optional<gc::Root<OpenBuffer>> BufferRegistry::Find(
    const BufferName& name) const {
  if (auto it = buffer_map_.find(name); it != buffer_map_.end())
    return it->second.Lock();
  return std::nullopt;
}

AnonymousBufferName BufferRegistry::NewAnonymousBufferName() {
  return AnonymousBufferName(next_anonymous_buffer_name_++);
}

std::vector<gc::Root<OpenBuffer>> BufferRegistry::buffers() const {
  std::vector<gc::Root<OpenBuffer>> output = container::MaterializeVector(
      buffer_map_ | std::views::values | gc::view::Lock);
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) {
        output.push_back(buffer.ToRoot());
      },
      [] {}, paste_);
  for (auto& buffer : retained_buffers_ | std::views::values)
    output.push_back(buffer.ToRoot());
  return output;
}

void BufferRegistry::Clear() {
  buffer_map_ = {};
  paste_ = std::nullopt;
  retained_buffers_ = {};
}

bool BufferRegistry::Remove(const BufferName& name) {
  retained_buffers_.erase(name);
  return buffer_map_.erase(name) > 0;
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
BufferRegistry::Expand() const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  VisitOptional(
      [&output](gc::Ptr<OpenBuffer> buffer) {
        output.push_back(buffer.object_metadata());
      },
      [] {}, paste_);
  for (auto& buffer : retained_buffers_ | std::views::values)
    output.push_back(buffer.object_metadata());
  return output;
}

void BufferRegistry::SetPaste(gc::Ptr<OpenBuffer> buffer) {
  paste_ = std::move(buffer);
}

std::optional<gc::Ptr<OpenBuffer>> BufferRegistry::paste() const {
  return paste_;
}
}  // namespace afc::editor
