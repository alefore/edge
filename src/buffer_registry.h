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
  // TODO(trivial, 2024-06-13): Handle paste_ identically to future_paste_:
  // don't require explicit `SetPaste` and `paste` methods, just use the regular
  // `Add` and `Find` methods.
  std::optional<language::gc::Ptr<OpenBuffer>> paste_;
  std::optional<language::gc::Ptr<OpenBuffer>> future_paste_;

  // Q: Why does this use WeakPtr (rather than Ptr)?
  // A: Buffers must find other ways to remain alive. Typically that is managed
  // through OpenBuffer::Options::SurvivalBehavior or explicit references
  // elsewhere.
  std::map<BufferName, language::gc::WeakPtr<OpenBuffer>> buffer_map_;

  size_t next_anonymous_buffer_name_ = 0;

 public:
  void SetPaste(language::gc::Ptr<OpenBuffer> buffer);
  std::optional<language::gc::Ptr<OpenBuffer>> paste() const;

  language::gc::Root<OpenBuffer> MaybeAdd(
      const BufferName& name,
      language::OnceOnlyFunction<language::gc::Root<OpenBuffer>()> factory);

  void Add(const BufferName& name, language::gc::WeakPtr<OpenBuffer> buffer);

  std::optional<language::gc::Root<OpenBuffer>> Find(
      const BufferName& name) const;

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
