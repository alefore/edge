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
  std::optional<language::gc::Ptr<OpenBuffer>> initial_commands_;

  std::optional<language::gc::Ptr<OpenBuffer>> paste_;

  std::map<BufferName, language::gc::Ptr<OpenBuffer>> buffer_map_;

  size_t next_anonymous_buffer_name_ = 0;

 public:
  void SetInitialCommands(language::gc::Ptr<OpenBuffer> buffer);

  void SetPaste(language::gc::Ptr<OpenBuffer> buffer);
  std::optional<language::gc::Ptr<OpenBuffer>> paste() const;

  language::gc::Ptr<OpenBuffer> MaybeAdd(
      const BufferName& name,
      language::OnceOnlyFunction<language::gc::Root<OpenBuffer>()> factory);

  void Add(const BufferName& name, language::gc::Ptr<OpenBuffer> buffer);

  std::optional<language::gc::Ptr<OpenBuffer>> Find(const BufferName& name);

  AnonymousBufferName NewAnonymousBufferName();

  // Return a vector containing all buffers.
  std::vector<language::gc::Ptr<OpenBuffer>> buffers() const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_REGISTRY_H__
