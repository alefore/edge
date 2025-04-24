#ifndef __AFC_EDITOR_BUFFER_REGISTRY_H__
#define __AFC_EDITOR_BUFFER_REGISTRY_H__

#include <map>
#include <optional>
#include <vector>

#include "src/buffer_name.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/once_only_function.h"

namespace afc::editor {
class OpenBuffer;
// This class is thread-safe.
class BufferRegistry {
  struct Data {
    // Q: Why are the values in buffer_map WeakPtr (rather than Ptr)?
    // A: Otherwise buffers would never be collected (unless they are explicitly
    // removed from the registry). Instead, we want to force their customers to
    // retain references explicitly. Typically that is managed through
    // OpenBuffer::Options::SurvivalBehavior (causing buffers to keep a
    // self-reference) or explicit references elsewhere.
    std::map<BufferName, language::gc::WeakPtr<OpenBuffer>> buffer_map;

    std::map<BufferName, language::gc::Ptr<OpenBuffer>> retained_buffers;

    std::vector<language::gc::WeakPtr<OpenBuffer>> buffers_with_screen;

    size_t next_anonymous_buffer_name = 0;
  };

  concurrent::Protected<Data> data_;

 public:
  language::gc::Root<OpenBuffer> MaybeAdd(
      const BufferName& name,
      language::OnceOnlyFunction<language::gc::Root<OpenBuffer>()> factory);

  void Add(const BufferName& name, language::gc::WeakPtr<OpenBuffer> buffer);

  std::optional<language::gc::Root<OpenBuffer>> Find(
      const BufferName& name) const;
  std::optional<language::gc::Root<OpenBuffer>> FindPath(
      const infrastructure::Path& path) const;

  AnonymousBufferName NewAnonymousBufferName();

  // Return a vector containing all buffers.
  std::vector<language::gc::Root<OpenBuffer>> buffers() const;

  // Return a vector containing all buffers that have registered a screen.
  std::vector<language::gc::Root<OpenBuffer>> BuffersWithScreen() const;
  void AddBufferWithScreen(language::gc::WeakPtr<OpenBuffer> buffer);

  void Clear();

  bool Remove(const BufferName& name);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  static void Add(Data& data, const BufferName& name,
                  language::gc::WeakPtr<OpenBuffer> buffer);
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_REGISTRY_H__
