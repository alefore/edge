#include "src/insert_history.h"

#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/search_handler.h"

using afc::language::Error;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::ValueOrError;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;

namespace afc::editor {

using ::operator<<;

void InsertHistory::Append(const LineSequence& insertion) {
  if (insertion.range().IsEmpty()) return;
  VLOG(5) << "Inserting to history: " << insertion.ToString();
  history_.push_back(insertion);
}

const std::vector<LineSequence>& InsertHistory::get() const { return history_; }

namespace {
bool IsMatch(EditorState& editor,
             const InsertHistory::SearchOptions& search_options,
             const LineSequence& candidate) {
  return std::visit(
      overload{[](std::vector<LineColumn> matches) { return !matches.empty(); },
               [](Error) { return false; }},
      SearchHandler(
          editor.modifiers().direction,
          afc::editor::SearchOptions{.search_query = search_options.query,
                                     .required_positions = 1,
                                     .case_sensitive = false},
          candidate));
}
}  // namespace

std::optional<LineSequence> InsertHistory::Search(
    EditorState& editor, InsertHistory::SearchOptions search_options) {
  if (history_.empty()) return std::nullopt;
  std::vector<LineSequence> matches;
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if (IsMatch(editor, search_options, *it)) matches.emplace_back(*it);
  }
  // TODO(2022-05-23): Sort matches with Bayes.
  if (!matches.empty()) return matches.front();
  return std::nullopt;
}

}  // namespace afc::editor
