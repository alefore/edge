#include "src/buffer_registry.h"

#include "src/buffer_variables.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc_expanders.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::EraseIf;
using afc::language::GetValueOrDefault;
using afc::language::IgnoreErrors;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::VisitOptional;

namespace afc::editor {
BufferRegistry::BufferRegistry(
    BufferRegistry::BufferComparePredicate listed_order,
    std::function<bool(const OpenBuffer&)> is_dirty,
    std::function<void(OpenBuffer&)> close_buffer)
    : data_(Data{.listed_order = std::move(listed_order)}),
      is_dirty_(std::move(is_dirty)),
      close_buffer_(std::move(close_buffer)) {
  CHECK(is_dirty_ != nullptr);
  CHECK(close_buffer_ != nullptr);
}

gc::Root<OpenBuffer> BufferRegistry::MaybeAdd(
    const BufferName& id, OnceOnlyFunction<gc::Root<OpenBuffer>()> factory) {
  struct RunFactory {};
  return std::visit(
      overload{[&](RunFactory) {
                 gc::Root<OpenBuffer> output = std::move(factory)();
                 data_.lock([&output, &id](Data& data,
                                           std::condition_variable& condition) {
                   EraseOrDie(data.buffers_under_construction, id);
                   Add(data, id, output.ptr().ToWeakPtr());
                   condition.notify_all();
                 });
                 return output;
               },
               [&](gc::Root<OpenBuffer> output) { return output; }},
      data_.wait_for_value(
          [id, &factory](Data& data)
              -> std::optional<std::variant<RunFactory, gc::Root<OpenBuffer>>> {
            if (data.buffers_under_construction.contains(id))
              return std::nullopt;
            if (auto it = data.buffer_map.find(id);
                it != data.buffer_map.end()) {
              if (std::optional<gc::Root<OpenBuffer>> output =
                      it->second.Lock();
                  output)
                return output.value();
              data.buffer_map.erase(it);
            }
            data.buffers_under_construction.insert(id);
            return RunFactory{};
          }));
}

void BufferRegistry::Add(const BufferName& name,
                         gc::WeakPtr<OpenBuffer> buffer) {
  data_.lock([&name, &buffer](Data& data, std::condition_variable&) {
    Add(data, name, buffer);
  });
}

void BufferRegistry::AddListedBuffer(gc::Root<OpenBuffer> buffer) {
  data_.lock([&](Data& data, std::condition_variable&) {
    if (std::find_if(data.listed_buffers.begin(), data.listed_buffers.end(),
                     [buffer_addr = &buffer.ptr().value()](
                         const gc::Root<OpenBuffer>& candidate) {
                       return &candidate.ptr().value() == buffer_addr;
                     }) != data.listed_buffers.end())
      return;
    data.listed_buffers.push_back(buffer);
    AdjustListedBuffers(data);
  });
}

void BufferRegistry::RemoveListedBuffers(
    const std::unordered_set<NonNull<const OpenBuffer*>>& buffers_to_erase) {
  data_.lock([&](Data& data, std::condition_variable&) {
    EraseIf(data.listed_buffers, [&buffers_to_erase](
                                     const gc::Root<OpenBuffer>& candidate) {
      return buffers_to_erase.contains(
          NonNull<const OpenBuffer*>::AddressOf(candidate.ptr().value()));
    });
    AdjustListedBuffers(data);
  });
}

std::optional<gc::Root<OpenBuffer>> BufferRegistry::Find(
    const BufferName& name) const {
  return data_.lock(
      [&name](const Data& data,
              std::condition_variable&) -> std::optional<gc::Root<OpenBuffer>> {
        if (auto it = data.buffer_map.find(name); it != data.buffer_map.end())
          return it->second.Lock();
        return std::nullopt;
      });
}

std::vector<gc::Root<OpenBuffer>> BufferRegistry::FindBuffersPathEndingIn(
    const Path& path) const {
  return data_.lock([&](const Data& data, std::condition_variable&) {
    return data.path_suffix_map.FindPathWithSuffix(path) |
           std::views::transform(
               [&data](const Path& buffer_path) -> gc::WeakPtr<OpenBuffer> {
                 if (auto it = data.buffer_map.find(BufferFileId{buffer_path});
                     it != data.buffer_map.end())
                   return it->second;
                 return gc::WeakPtr<OpenBuffer>();
               }) |
           gc::view::Lock | std::ranges::to<std::vector>();
  });
}

std::optional<gc::Root<OpenBuffer>> BufferRegistry::FindPath(
    const infrastructure::Path& path) const {
  return Find(BufferFileId{path});
}

AnonymousBufferName BufferRegistry::NewAnonymousBufferName() {
  return AnonymousBufferName(
      data_.lock([](Data& data, std::condition_variable&) {
        return data.next_anonymous_buffer_name++;
      }));
}

std::vector<gc::Root<OpenBuffer>> BufferRegistry::buffers() const {
  return data_.lock([](const Data& data, std::condition_variable&) {
    return data.buffer_map | std::views::values | gc::view::Lock |
           std::ranges::to<std::vector>();
  });
}

std::vector<language::gc::Root<OpenBuffer>> BufferRegistry::BuffersWithScreen()
    const {
  return data_.lock([](const Data& data, std::condition_variable&) {
    return data.buffers_with_screen | gc::view::Lock |
           std::ranges::to<std::vector>();
  });
}

void BufferRegistry::AddBufferWithScreen(
    language::gc::WeakPtr<OpenBuffer> buffer) {
  data_.lock([&buffer](Data& data, std::condition_variable&) {
    data.buffers_with_screen.push_back(buffer);
  });
}

void BufferRegistry::Clear() {
  return data_.lock([](Data& data, std::condition_variable&) {
    data.buffer_map = {};
    data.retained_buffers = {};
    data.buffers_with_screen = {};
    data.listed_buffers.clear();
  });
}

bool BufferRegistry::Remove(const BufferName& name) {
  return data_.lock([&name](Data& data, std::condition_variable&) {
    data.retained_buffers.erase(name);
    if (const auto* id = std::get_if<BufferFileId>(&name); id != nullptr)
      data.path_suffix_map.Erase(id->read());
    return data.buffer_map.erase(name) > 0;
  });
}

size_t BufferRegistry::ListedBuffersCount() const {
  return data_.lock([](const Data& data, std::condition_variable&) {
    return data.listed_buffers.size();
  });
}

language::gc::Root<OpenBuffer> BufferRegistry::GetListedBuffer(
    size_t index) const {
  return data_.lock([index](const Data& data, std::condition_variable&) {
    return data.listed_buffers[index % data.listed_buffers.size()];
  });
}

std::optional<size_t> BufferRegistry::GetListedBufferIndex(
    const OpenBuffer& buffer) const {
  return data_.lock([&buffer](
                        const Data& data,
                        std::condition_variable&) -> std::optional<size_t> {
    if (auto it =
            std::find_if(data.listed_buffers.begin(), data.listed_buffers.end(),
                         [&buffer](const gc::Root<OpenBuffer>& candidate) {
                           return &candidate.ptr().value() == &buffer;
                         });
        it != data.listed_buffers.end())
      return std::distance(data.listed_buffers.begin(), it);
    return std::nullopt;
  });
}

void BufferRegistry::Expand(gc::ObjectMetadata::Receiver& visit) const {
  return data_.lock([&visit](const Data& data, std::condition_variable&) {
    LOG(INFO) << "BufferRegistry: Expand: " << data.retained_buffers.size();
    visit.all(data.retained_buffers | std::views::values);
  });
}

void BufferRegistry::SetListedCount(std::optional<size_t> value) {
  data_.lock([value](Data& data, std::condition_variable&) {
    data.listed_count = value;
  });
}

std::optional<size_t> BufferRegistry::listed_count() const {
  return data_.lock([](const Data& data, std::condition_variable&) {
    return data.listed_count;
  });
}

void BufferRegistry::SetShownCount(std::optional<size_t> value) {
  data_.lock([value, this](Data& data, std::condition_variable&) {
    data.shown_count = value;
    AdjustListedBuffers(data);
  });
}

std::optional<size_t> BufferRegistry::shown_count() const {
  return data_.lock([](const Data& data, std::condition_variable&) {
    return data.shown_count;
  });
}

void BufferRegistry::SetListedSortOrder(BufferComparePredicate predicate) {
  return data_.lock([&predicate](Data& data, std::condition_variable&) {
    data.listed_order = predicate;
  });
}

/* static */
void BufferRegistry::Add(Data& data, const BufferName& name,
                         gc::WeakPtr<OpenBuffer> buffer) {
  LOG(INFO) << "Adding buffer: " << name;
  // TODO(2024-05-30, trivial): Detect errors if a server already was there.
  if (std::holds_alternative<FuturePasteBuffer>(name) ||
      std::holds_alternative<HistoryBufferName>(name) ||
      std::holds_alternative<PasteBuffer>(name) ||
      std::holds_alternative<FragmentsBuffer>(name) ||
      std::holds_alternative<ServerBufferName>(name)) {
    LOG(INFO) << "Retain buffer: " << name;
    data.retained_buffers.insert_or_assign(name, buffer.Lock()->ptr());
  }
  if (const auto* id = std::get_if<BufferFileId>(&name); id != nullptr)
    data.path_suffix_map.Insert(id->read());
  data.buffer_map.insert_or_assign(name, std::move(buffer));
}

void BufferRegistry::AdjustListedBuffers(Data& data) {
  std::sort(data.listed_buffers.begin(), data.listed_buffers.end(),
            [order = std::ref(data.listed_order)](
                const gc::Root<OpenBuffer>& a,
                const gc::Root<OpenBuffer>& b) -> bool {
              return order(a.ptr().value(), b.ptr().value()) < 0;
            });

  VisitOptional(
      [&](size_t limit) {
        if (data.listed_buffers.size() > limit) {
          std::vector<gc::Root<OpenBuffer>> retained_buffers;
          retained_buffers.reserve(data.listed_buffers.size());
          for (size_t index = 0; index < data.listed_buffers.size(); ++index) {
            if (index < limit ||
                is_dirty_(data.listed_buffers[index].ptr().value())) {
              retained_buffers.push_back(std::move(data.listed_buffers[index]));
            } else {
              close_buffer_(data.listed_buffers[index].value());
            }
          }
          data.listed_buffers = std::move(retained_buffers);
        }
      },
      [] { /* Nothing. */
      },
      data.listed_count);
}
}  // namespace afc::editor
