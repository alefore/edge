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
                                 language::lazy_string::LazyString> {
  using GhostType::GhostType;
};

struct DictionaryValue
    : public language::GhostType<DictionaryValue,
                                 language::lazy_string::LazyString> {
  using GhostType::GhostType;
};

class DictionaryManager {
 public:
  struct NothingFound {};

  using BufferLoader =
      std::function<futures::Value<language::text::LineSequence>(
          infrastructure::Path)>;
  DictionaryManager(BufferLoader buffer_loader);

  // We return:
  // * A DictionaryValue if we want to suggest that the key should be
  //   expanded to a given value.
  // * A DictionaryKey if we want to suggest that the user should have typed a
  //   different (shorter) key to produce the key given. In other words, the
  //   output key would have expanded to the input.
  using QueryOutput =
      std::variant<DictionaryValue, DictionaryKey, NothingFound>;
  futures::Value<QueryOutput> Query(std::vector<infrastructure::Path> models,
                                    DictionaryKey key);

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
      std::shared_ptr<std::vector<infrastructure::Path>> models,
      DictionaryKey key, size_t index);

  static void UpdateReverseTable(Data& data, const infrastructure::Path& path,
                                 const language::text::LineSequence& contents);

  struct Data {
    ModelsMap models;
    std::map<DictionaryValue, std::map<infrastructure::Path, DictionaryKey>>
        reverse_table;
  };

  const BufferLoader buffer_loader_;
  const language::NonNull<std::shared_ptr<concurrent::Protected<Data>>> data_ =
      language::MakeNonNullShared<concurrent::Protected<Data>>();
};

}  // namespace afc::editor
#endif  // __AFC_EDITOR_COMPLETION_MODEL_H__
