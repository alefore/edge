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
class BufferRegistry {
  // Q: Why does this use WeakPtr (rather than Ptr)?
  // A: Buffers must find other ways to remain alive. Typically that is managed
  // through OpenBuffer::Options::SurvivalBehavior or explicit references
  // elsewhere.
  std::map<BufferName, language::gc::WeakPtr<OpenBuffer>> buffer_map_;

  std::map<BufferName, language::gc::Ptr<OpenBuffer>> retained_buffers_;

  size_t next_anonymous_buffer_name_ = 0;

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

  void Clear();

  bool Remove(const BufferName& name);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_REGISTRY_H__
