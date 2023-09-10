#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "src/futures/delete_notification.h"
#include "src/language/safe_types.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/line_prompt_mode.h"
#include "src/predictor.h"

namespace afc {
namespace editor {
class OpenBuffer;

futures::Value<PredictorOutput> SearchHandlerPredictor(PredictorInput input);

// All fields here must be thread-compatible. Const values of this will be given
// to background threads.
struct SearchOptions {
  // The position in which to start searching for positions.
  language::text::LineColumn starting_position = {};

  // The regular expression to search.
  std::wstring search_query;

  // An optional position where the search should stop.
  std::optional<language::text::LineColumn> limit_position = std::nullopt;

  // If set, signals that it is okay for the search operation to stop once this
  // number of positions has been found.
  std::optional<size_t> required_positions;

  // When notified, interrupts the search.
  futures::DeleteNotification::Value abort_value =
      futures::DeleteNotification::Never();

  bool case_sensitive;
};

// TODO(easy, 2023-09-09): Document why we receive a work_queue.
language::ValueOrError<std::vector<language::text::LineColumn>> SearchHandler(
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue,
    Direction direction, const SearchOptions& options,
    const language::text::MutableLineSequence& buffer);

void HandleSearchResults(
    const language::ValueOrError<std::vector<language::text::LineColumn>>&
        results_or_error,
    OpenBuffer& buffer);

language::ValueOrError<language::text::LineColumn> GetNextMatch(
    language::NonNull<std::shared_ptr<concurrent::WorkQueue>> work_queue,
    Direction direction, const SearchOptions& options,
    const language::text::MutableLineSequence& contents);

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
                         const language::text::MutableLineSequence& contents,
                         ProgressChannel&);

}  // namespace editor
}  // namespace afc

#endif
