#ifndef __AFC_EDITOR_BUFFER_REGISTRY_H__
#define __AFC_EDITOR_BUFFER_REGISTRY_H__

#include <compare>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <vector>

#include "src/buffer_name.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/path_suffix_map.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/once_only_function.h"

namespace afc::editor {
class OpenBuffer;
// This class is thread-safe.
class BufferRegistry {
 public:
  using BufferComparePredicate =
      std::function<std::weak_ordering(const OpenBuffer&, const OpenBuffer&)>;

 private:
  struct Data {
    // Q: Why are the values in buffer_map WeakPtr (rather than Ptr)?
    // A: Otherwise buffers would never be collected (unless they are explicitly
    // removed from the registry). Instead, we want to force their customers to
    // retain references explicitly. Typically that is managed through
    // OpenBuffer::Options::SurvivalBehavior (causing buffers to keep a
    // self-reference) or explicit references elsewhere.
    std::map<BufferName, language::gc::WeakPtr<OpenBuffer>> buffer_map = {};

    std::map<BufferName, language::gc::Ptr<OpenBuffer>> retained_buffers = {};

    std::vector<language::gc::WeakPtr<OpenBuffer>> buffers_with_screen = {};

    std::vector<language::gc::Root<OpenBuffer>> listed_buffers = {};

    infrastructure::PathSuffixMap path_suffix_map = {};

    size_t next_anonymous_buffer_name = 0;

    // Previously known as buffers_to_retain_.
    std::optional<size_t> listed_count = std::nullopt;
    // Must always be >0. Previously known as buffers_to_show_.
    std::optional<size_t> shown_count = std::nullopt;

    BufferComparePredicate listed_order;
  };

  concurrent::Protected<Data> data_;

  // Calls OpenBuffer::dirty. We have to wrap this to avoid a circular
  // dependency from BufferRegistry into OpenBuffer. Ugly.
  const std::function<bool(const OpenBuffer&)> is_dirty_;

 public:
  BufferRegistry(BufferComparePredicate listed_order,
                 std::function<bool(const OpenBuffer&)> is_dirty);

  language::gc::Root<OpenBuffer> MaybeAdd(
      const BufferName& name,
      language::OnceOnlyFunction<language::gc::Root<OpenBuffer>()> factory);

  void Add(const BufferName& name, language::gc::WeakPtr<OpenBuffer> buffer);

  void AddListedBuffer(language::gc::Root<OpenBuffer> buffer);
  void RemoveListedBuffers(
      const std::unordered_set<language::NonNull<const OpenBuffer*>>&
          buffers_to_erase);

  std::optional<language::gc::Root<OpenBuffer>> Find(
      const BufferName& name) const;

  std::vector<language::gc::Root<OpenBuffer>> FindBuffersPathEndingIn(
      const infrastructure::Path& path) const;

  std::optional<language::gc::Root<OpenBuffer>> FindPath(
      const infrastructure::Path& path) const;

  AnonymousBufferName NewAnonymousBufferName();

  // Return a vector containing all buffers.
  std::vector<language::gc::Root<OpenBuffer>> buffers() const;

  // Return a vector containing all buffers that have registered a screen.
  std::vector<language::gc::Root<OpenBuffer>> BuffersWithScreen() const;
  void AddBufferWithScreen(language::gc::WeakPtr<OpenBuffer> buffer);

  template <typename Callable>
  decltype(auto) LockListedBuffers(Callable callable) {
    return data_.lock([&callable](Data& data) {
      return std::move(callable)(data.listed_buffers);
    });
  }

  void Clear();

  bool Remove(const BufferName& name);

  size_t ListedBuffersCount() const;
  language::gc::Root<OpenBuffer> GetListedBuffer(size_t index) const;
  std::optional<size_t> GetListedBufferIndex(const OpenBuffer& buffer) const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

  void SetListedCount(std::optional<size_t> value);
  std::optional<size_t> listed_count() const;

  // Must always be >0.
  void SetShownCount(std::optional<size_t> value);
  std::optional<size_t> shown_count() const;

  void SetListedSortOrder(BufferComparePredicate listed_order);

 private:
  static void Add(Data& data, const BufferName& name,
                  language::gc::WeakPtr<OpenBuffer> buffer);

  void AdjustListedBuffers(Data& data);
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_REGISTRY_H__
