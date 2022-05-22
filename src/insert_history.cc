#include "src/insert_history.h"

#include "src/language/wstring.h"

namespace afc::editor {
using language::NonNull;
using ::operator<<;

void InsertHistory::Append(const BufferContents& insertion) {
  if (insertion.range().IsEmpty()) return;
  VLOG(5) << "Inserting to history: " << insertion.ToString();
  history_.push_back(insertion.copy());
}

namespace {
bool IsMatch(const InsertHistory::SearchOptions& search_options,
             const BufferContents& candidate) {
  return true;
}
}  // namespace

std::optional<NonNull<const BufferContents*>> InsertHistory::Search(
    InsertHistory::SearchOptions search_options) {
  if (history_.empty()) return std::nullopt;
  std::vector<NonNull<const BufferContents*>> matches;
  for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
    if (IsMatch(search_options, it->value()))
      matches.push_back(NonNull<const BufferContents*>::AddressOf(it->value()));
  }
  // TODO(2022-05-23): Sort matches with Bayes.
  if (!matches.empty()) return matches.front();
  return std::nullopt;
}

}  // namespace afc::editor
