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
  return data_.lock([id, &factory](Data& data) {
    // TODO(trivial, 2024-05-30): Only traverse the map once.
    if (auto it = data.buffer_map.find(id); it != data.buffer_map.end())
      if (std::optional<gc::Root<OpenBuffer>> previous_buffer =
              it->second.Lock();
          previous_buffer.has_value())
        return previous_buffer.value();

    gc::Root<OpenBuffer> buffer = std::move(factory)();
    Add(data, id, buffer.ptr().ToWeakPtr());
    return buffer;
  });
}

void BufferRegistry::Add(const BufferName& name,
                         gc::WeakPtr<OpenBuffer> buffer) {
  data_.lock([&name, &buffer](Data& data) { Add(data, name, buffer); });
}

std::optional<gc::Root<OpenBuffer>> BufferRegistry::Find(
    const BufferName& name) const {
  return data_.lock(
      [&name](const Data& data) -> std::optional<gc::Root<OpenBuffer>> {
        if (auto it = data.buffer_map.find(name); it != data.buffer_map.end())
          return it->second.Lock();
        return std::nullopt;
      });
}

std::optional<gc::Root<OpenBuffer>> BufferRegistry::FindPath(
    const infrastructure::Path& path) const {
  return Find(BufferFileId{path});
}

AnonymousBufferName BufferRegistry::NewAnonymousBufferName() {
  return AnonymousBufferName(
      data_.lock([](Data& data) { return data.next_anonymous_buffer_name++; }));
}

std::vector<gc::Root<OpenBuffer>> BufferRegistry::buffers() const {
  return data_.lock([](const Data& data) {
    return container::MaterializeVector(data.buffer_map | std::views::values |
                                        gc::view::Lock);
  });
}

void BufferRegistry::Clear() {
  return data_.lock([](Data& data) {
    data.buffer_map = {};
    data.retained_buffers = {};
  });
}

bool BufferRegistry::Remove(const BufferName& name) {
  return data_.lock([&name](Data& data) {
    data.retained_buffers.erase(name);
    return data.buffer_map.erase(name) > 0;
  });
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>>
BufferRegistry::Expand() const {
  return data_.lock([](const Data& data) {
    return container::MaterializeVector(
        data.retained_buffers | std::views::values | gc::view::ObjectMetadata);
  });
}

/* static */
void BufferRegistry::Add(Data& data, const BufferName& name,
                         gc::WeakPtr<OpenBuffer> buffer) {
  // TODO(2024-05-30, trivial): Detect errors if a server already was there.
  if (std::holds_alternative<FuturePasteBuffer>(name) ||
      std::holds_alternative<HistoryBufferName>(name) ||
      std::holds_alternative<PasteBuffer>(name) ||
      std::holds_alternative<FragmentsBuffer>(name))
    data.retained_buffers.insert_or_assign(name, buffer.Lock()->ptr());
  data.buffer_map.insert_or_assign(name, std::move(buffer));
}
}  // namespace afc::editor
