#include "src/buffer_registry.h"

#include "src/buffer_variables.h"
#include "src/language/container.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc_view.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/once_only_function.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/tests/tests.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::language::GetValueOrDefault;
using afc::language::IgnoreErrors;
using afc::language::IsError;
using afc::language::NonNull;
using afc::language::OnceOnlyFunction;
using afc::language::overload;
using afc::language::ValueOrError;
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

namespace {
template <typename T>
bool EndsIn(std::list<T> suffix, std::list<T> candidate) {
  if (candidate.size() < suffix.size()) return false;
  auto candidate_it = candidate.begin();
  std::advance(candidate_it, candidate.size() - suffix.size());
  for (const auto& t : suffix) {
    if (*candidate_it != t) return false;
    ++candidate_it;
  }
  return true;
}
const bool ends_in_registration = tests::Register(
    L"EndsIn",
    {
        {.name = L"EmptyBoth", .callback = [] { CHECK(EndsIn<int>({}, {})); }},
        {.name = L"EmptySuffix",
         .callback = [] { CHECK(EndsIn<int>({}, {1, 2, 3})); }},
        {.name = L"EmptyCandidate",
         .callback = [] { CHECK(!EndsIn<int>({1, 2, 3}, {})); }},
        {.name = L"ShortNonEmptyCandidate",
         .callback = [] { CHECK(!EndsIn<int>({1, 2, 3}, {1, 2})); }},
        {.name = L"IdenticalNonEmpty",
         .callback = [] { CHECK(EndsIn<int>({1, 2, 3}, {1, 2, 3})); }},
        {.name = L"EndsInLongerCandidate",
         .callback = [] { CHECK(EndsIn<int>({4, 5, 6}, {1, 2, 3, 4, 5, 6})); }},
        {.name = L"NotEndsInLongerCandidate",
         .callback =
             [] { CHECK(!EndsIn<int>({4, 5, 6}, {1, 2, 3, 4, 0, 6})); }},
    });
}  // namespace

std::vector<gc::Root<OpenBuffer>> BufferRegistry::FindBuffersPathEndingIn(
    std::list<PathComponent> path_components) const {
  return data_.lock([&](const Data& data) {
    std::vector<gc::Root<OpenBuffer>> output;
    // TODO(2025-05-15, easy): Optimize this to not have N^2 complexity (where
    // N is the number of buffers). The best way to achieve that is probably to
    // keep a separate index:
    //
    //     std::multimap<std::list<PathComponent>, gc::WeakPtr<OpenBuffer>>.
    //
    // The keys are suffixes. "/home/alejo/foo" would have entries like {"foo"},
    // {"alejo", "foo"}, {"home", "alejo", "foo"}.
    for (const std::pair<const BufferName, gc::WeakPtr<OpenBuffer>>& item :
         data.buffer_map) {
      TRACK_OPERATION(FindAlreadyOpenBuffer_InnerLoop_Iteration);
      std::optional<gc::Root<OpenBuffer>> buffer = item.second.Lock();
      if (!buffer.has_value()) continue;
      if (const BufferFileId* buffer_path =
              std::get_if<BufferFileId>(&item.first);
          buffer_path != nullptr)
        std::visit(overload{IgnoreErrors{},
                            [&path_components, &output, &buffer](
                                std::list<PathComponent> buffer_components) {
                              if (EndsIn(path_components, buffer_components))
                                output.push_back(buffer.value());
                            }},
                   buffer_path->read().DirectorySplit());
    }
    return output;
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

std::vector<language::gc::Root<OpenBuffer>> BufferRegistry::BuffersWithScreen()
    const {
  return data_.lock([](const Data& data) {
    return container::MaterializeVector(data.buffers_with_screen |
                                        gc::view::Lock);
  });
}

void BufferRegistry::AddBufferWithScreen(
    language::gc::WeakPtr<OpenBuffer> buffer) {
  data_.lock(
      [&buffer](Data& data) { data.buffers_with_screen.push_back(buffer); });
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
