#include "src/insert_history.h"

namespace afc::editor {
using language::NonNull;

void InsertHistory::Append(const BufferContents& insertion) {
  history_.push_back(insertion.copy());
}

std::optional<NonNull<const BufferContents*>> InsertHistory::Search(
    InsertHistory::SearchOptions) {
  if (history_.empty()) return std::nullopt;
  return NonNull<const BufferContents*>::AddressOf(history_.back().value());
}

}  // namespace afc::editor
