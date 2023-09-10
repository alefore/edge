#ifndef __AFC_EDITOR_INSERT_HISTORY_H__
#define __AFC_EDITOR_INSERT_HISTORY_H__

#include <memory>
#include <optional>
#include <vector>

#include "src/language/safe_types.h"
#include "src/language/text/line_sequence.h"

namespace afc::editor {
class EditorState;

class InsertHistory {
 public:
  InsertHistory() = default;

  void Append(const language::text::MutableLineSequence& insertion);

  const std::vector<
      language::NonNull<std::unique_ptr<const language::text::MutableLineSequence>>>&
  get() const;

  struct SearchOptions {
    std::wstring query;
  };

  // Return the entry from the history that best fits `search_options`. For now,
  // that's just the most recent entry.
  std::optional<language::NonNull<const language::text::MutableLineSequence*>> Search(
      EditorState& editor, SearchOptions search_options);

 private:
  std::vector<
      language::NonNull<std::unique_ptr<const language::text::MutableLineSequence>>>
      history_;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_INSERT_HISTORY_H__
