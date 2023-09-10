#ifndef __AFC_EDITOR_COMPLETION_MODEL_H__
#define __AFC_EDITOR_COMPLETION_MODEL_H__

#include <memory>

#include "src/concurrent/protected.h"
#include "src/futures/futures.h"
#include "src/futures/listenable_value.h"
#include "src/infrastructure/dirname.h"
#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
class CompletionModelManager {
 public:
  // TODO(templates, 2023-09-02): Use GHOST_TYPE. That is tricky because we need
  // to be able to selectively disable some constructors, which requires finicky
  // SFINAE. And operator<<.
  using CompressedText =
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>;
  using Text =
      language::NonNull<std::shared_ptr<language::lazy_string::LazyString>>;

  struct NothingFound {};

  struct Suggestion {
    CompressedText compressed_text;
  };

  using BufferLoader =
      std::function<futures::Value<language::text::LineSequence>(
          infrastructure::Path)>;
  CompletionModelManager(BufferLoader buffer_loader);

  using QueryOutput = std::variant<Text, Suggestion, NothingFound>;
  futures::Value<QueryOutput> Query(std::vector<infrastructure::Path> models,
                                    CompressedText compressed_text);

 private:
  using CompletionModel = language::text::LineSequence;
  using ModelsMap =
      std::map<infrastructure::Path, futures::ListenableValue<CompletionModel>>;

  struct Data;

  static futures::Value<QueryOutput> FindCompletionWithIndex(
      BufferLoader buffer_loader,
      language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data,
      std::shared_ptr<std::vector<infrastructure::Path>> models,
      CompressedText compressed_text, size_t index);

  static void UpdateReverseTable(Data& data, const infrastructure::Path& path,
                                 const language::text::LineSequence& contents);

  struct Data {
    ModelsMap models;
    std::map<std::wstring, std::map<infrastructure::Path, CompressedText>>
        reverse_table;
  };

  const BufferLoader buffer_loader_;
  const language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data_ =
      language::MakeNonNullShared<concurrent::Protected<Data>>();
};

}  // namespace afc::editor
#endif  // __AFC_EDITOR_COMPLETION_MODEL_H__
