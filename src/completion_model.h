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
struct DictionaryKey
    : public language::GhostType<DictionaryKey,
                                 language::lazy_string::LazyString> {};

class DictionaryManager {
 public:
  struct WordData {
    using Text = language::lazy_string::LazyString;
    std::optional<Text> replacement;

    bool operator==(const WordData&) const;
  };

  // TODO(trivial, 2024-08-25): Remove this `using` declaration?
  using Key = DictionaryKey;

  struct NothingFound {};

  struct Suggestion {
    Key key;
  };

  using BufferLoader =
      std::function<futures::Value<language::text::LineSequence>(
          infrastructure::Path)>;
  DictionaryManager(BufferLoader buffer_loader);

  using QueryOutput = std::variant<WordData, Suggestion, NothingFound>;
  futures::Value<QueryOutput> Query(std::vector<infrastructure::Path> models,
                                    Key key);

 private:
  using DictionaryInput = language::text::SortedLineSequence;
  using ModelsMap =
      std::map<infrastructure::Path, futures::ListenableValue<DictionaryInput>>;

  struct Data;

  // index is an index into the `models` vector; the semantics are that we
  // should start the search at that position (and iterate until the end of
  // `models`, or until we find something).
  static futures::Value<QueryOutput> FindWordDataWithIndex(
      BufferLoader buffer_loader,
      language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data,
      std::shared_ptr<std::vector<infrastructure::Path>> models, Key key,
      size_t index);

  static void UpdateReverseTable(Data& data, const infrastructure::Path& path,
                                 const language::text::LineSequence& contents);

  struct Data {
    ModelsMap models;
    std::map<std::wstring, std::map<infrastructure::Path, Key>> reverse_table;
  };

  const BufferLoader buffer_loader_;
  const language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data_ =
      language::MakeNonNullShared<concurrent::Protected<Data>>();
};

}  // namespace afc::editor
#endif  // __AFC_EDITOR_COMPLETION_MODEL_H__
