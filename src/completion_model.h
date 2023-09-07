#ifndef __AFC_EDITOR_COMPLETION_MODEL_H__
#define __AFC_EDITOR_COMPLETION_MODEL_H__

#include <memory>

#include "src/buffer.h"
#include "src/concurrent/protected.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"

namespace afc::editor::completion {

// TODO(templates, 2023-09-02): Use GHOST_TYPE. That is tricky because we need
// to be able to selectively disable some constructors, which requires finicky
// SFINAE. And operator<<.
using CompressedText =
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>;
using Text =
    language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>;

class ModelSupplier {
 public:
  ModelSupplier(EditorState& editor);

  futures::Value<std::optional<Text>> FindCompletion(
      std::vector<infrastructure::Path> models, CompressedText compressed_text);

 private:
  using CompletionModel = language::gc::Root<OpenBuffer>;
  using ModelsMap = concurrent::Protected<std::map<
      infrastructure::Path, futures::ListenableValue<CompletionModel>>>;

  /* static */ futures::Value<std::optional<Text>> FindCompletionWithIndex(
      std::shared_ptr<std::vector<infrastructure::Path>> models,
      CompressedText compressed_text, size_t index,
      language::NonNull<std::shared_ptr<ModelsMap>> models_map);

  EditorState& editor_;

  const language::NonNull<std::shared_ptr<ModelsMap>> models_ =
      language::MakeNonNullShared<ModelsMap>();
};

}  // namespace afc::editor::completion
#endif  // __AFC_EDITOR_COMPLETION_MODEL_H__
