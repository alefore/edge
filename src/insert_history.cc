#include "src/insert_history.h"

#include "src/language/wstring.h"
#include "src/search_handler.h"

namespace afc::editor {
using language::NonNull;
using language::ValueOrError;
using ::operator<<;

void InsertHistory::Append(const BufferContents& insertion) {
  if (insertion.range().IsEmpty()) return;
  VLOG(5) << "Inserting to history: " << insertion.ToString();
  history_.push_back(insertion.copy());
}

const std::vector<language::NonNull<std::unique_ptr<const BufferContents>>>&
InsertHistory::get() const {
  return history_;
}

namespace {
bool IsMatch(EditorState& editor,
             const InsertHistory::SearchOptions& search_options,
             const BufferContents& candidate) {
  ValueOrError<std::vector<LineColumn>> matches = SearchHandler(
      editor,
      afc::editor::SearchOptions{.search_query = search_options.query,
                                 .required_positions = 1,
                                 .case_sensitive = false},
      candidate);
  std::vector<LineColumn>* matches_vector =
      std::get_if<std::vector<LineColumn>>(&matches);
  return matches_vector != nullptr && !matches_vector->empty();
}
}  // namespace

std::optional<NonNull<const BufferContents*>> InsertHistory::Search(
    EditorState& editor, InsertHistory::SearchOptions search_options) {
  if (history_.empty()) return std::nullopt;
  std::vector<NonNull<const BufferContents*>> matches;
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if (IsMatch(editor, search_options, it->value()))
      matches.push_back(NonNull<const BufferContents*>::AddressOf(it->value()));
  }
  // TODO(2022-05-23): Sort matches with Bayes.
  if (!matches.empty()) return matches.front();
  return std::nullopt;
}

}  // namespace afc::editor
