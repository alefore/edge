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

std::optional<NonNull<const BufferContents*>> InsertHistory::Search(
    InsertHistory::SearchOptions) {
  if (history_.empty()) return std::nullopt;
  return NonNull<const BufferContents*>::AddressOf(history_.back().value());
}

}  // namespace afc::editor
