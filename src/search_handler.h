#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "src/buffer_contents.h"
#include "src/futures/futures.h"
#include "src/line_column.h"
#include "src/line_prompt_mode.h"
#include "src/notification.h"
#include "src/predictor.h"

namespace afc {
namespace editor {

using std::wstring;

class EditorState;
class OpenBuffer;
struct LineColumn;
class ThreadPool;

futures::Value<PredictorOutput> SearchHandlerPredictor(PredictorInput input);

// All fields here must be thread-compatible. Const values of this will be given
// to background threads.
struct SearchOptions {
  // The position in which to start searching for positions.
  LineColumn starting_position = {};

  // The regular expression to search.
  wstring search_query;

  // An optional position where the search should stop.
  std::optional<LineColumn> limit_position = std::nullopt;

  // If set, signals that it is okay for the search operation to stop once this
  // number of positions has been found.
  std::optional<size_t> required_positions;

  // When notified, interrupts the search. Must not be nullptr.
  std::shared_ptr<Notification> abort_notification =
      std::make_shared<Notification>();
};

std::vector<LineColumn> SearchHandler(EditorState& editor_state,
                                      const SearchOptions& options,
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

  std::wstring ToString() const;
};

// Return a callback that is safe to run in a background thread to count the
// number of matches, feeding information to a `ProgressChannel`.
//
// Customer must ensure that `progress_channel` survives until the callback has
// returned (but it's OK for `buffer` to be deleted as soon as
// `BackgroundSearchCallback` has returned).
std::function<ValueOrError<SearchResultsSummary>()> BackgroundSearchCallback(
    SearchOptions search_options, const OpenBuffer& buffer, ProgressChannel&);

}  // namespace editor
}  // namespace afc

#endif
