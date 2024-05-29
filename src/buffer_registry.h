#ifndef __AFC_EDITOR_BUFFER_REGISTRY_H__
#define __AFC_EDITOR_BUFFER_REGISTRY_H__

#include <map>
#include <optional>

#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"

namespace afc::editor {
class OpenBuffer;
class BufferRegistry {
  std::optional<language::gc::Ptr<OpenBuffer>> initial_commands_;

  std::optional<language::gc::Ptr<OpenBuffer>> paste_;

  std::map<infrastructure::Path, language::gc::Ptr<OpenBuffer>> files_;

 public:
  void SetInitialCommands(language::gc::Ptr<OpenBuffer> buffer);

  void SetPaste(language::gc::Ptr<OpenBuffer> buffer);
  std::optional<language::gc::Ptr<OpenBuffer>> paste() const;

  void AddFile(infrastructure::Path path, language::gc::Ptr<OpenBuffer> buffer);

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_REGISTRY_H__
