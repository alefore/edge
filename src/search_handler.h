#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "src/buffer_contents.h"
#include "src/concurrent/notification.h"
#include "src/futures/futures.h"
#include "src/language/safe_types.h"
#include "src/line_column.h"
#include "src/line_prompt_mode.h"
#include "src/predictor.h"

namespace afc {
namespace concurrent {
class Notification;
}
namespace editor {
class EditorState;
class OpenBuffer;
struct LineColumn;

futures::Value<PredictorOutput> SearchHandlerPredictor(PredictorInput input);

// All fields here must be thread-compatible. Const values of this will be given
// to background threads.
struct SearchOptions {
  // The position in which to start searching for positions.
  LineColumn starting_position = {};

  // The regular expression to search.
  std::wstring search_query;

  // An optional position where the search should stop.
  std::optional<LineColumn> limit_position = std::nullopt;

  // If set, signals that it is okay for the search operation to stop once this
  // number of positions has been found.
  std::optional<size_t> required_positions;

  // When notified, interrupts the search.
  language::NonNull<std::shared_ptr<concurrent::Notification>>
      abort_notification = {};

  bool case_sensitive;
};

language::ValueOrError<std::vector<LineColumn>> SearchHandler(
    EditorState& editor_state, const SearchOptions& options,
    const BufferContents& buffer);

void HandleSearchResults(
    const language::ValueOrError<std::vector<LineColumn>>& results_or_error,
    OpenBuffer& buffer);

void JumpToNextMatch(EditorState& editor_state, const SearchOptions& options,
                     OpenBuffer& buffer);

struct SearchResultsSummary {
  size_t matches = 0;
  enum class SearchCompletion {
    // The search was interrupted. It's possible that there are more matches.
    kInterrupted,
    // The search consumed the entire input.
    kFull,
  };
  SearchCompletion search_completion = SearchCompletion::kFull;
};

std::ostream& operator<<(std::ostream& os, const SearchResultsSummary& a);
bool operator==(const SearchResultsSummary& a, const SearchResultsSummary& b);

// Return a callback that is safe to run in a background thread to count the
// number of matches, feeding information to a `ProgressChannel`.
//
// Customer must ensure that `progress_channel` survives until the callback has
// returned. It's OK for `contents` to be deleted as soon as
// `BackgroundSearchCallback` has returned).
std::function<language::ValueOrError<SearchResultsSummary>()>
BackgroundSearchCallback(SearchOptions search_options,
                         const BufferContents& contents, ProgressChannel&);

}  // namespace editor
}  // namespace afc

#endif
