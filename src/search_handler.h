#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <vector>

#include "src/futures/delete_notification.h"
#include "src/language/lazy_string/lazy_string.h"
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
  language::lazy_string::LazyString search_query;

  // An optional position where the search should stop.
  std::optional<language::text::LineColumn> limit_position = std::nullopt;

  // If set, signals that it is okay for the search operation to stop once this
  // number of positions has been found.
  std::optional<size_t> required_positions = std::nullopt;

  // When notified, interrupts the search.
  futures::DeleteNotification::Value abort_value =
      futures::DeleteNotification::Never();

  bool case_sensitive = false;

  language::NonNull<std::shared_ptr<concurrent::Channel<ProgressInformation>>>
      progress_channel = language::MakeNonNullShared<
          concurrent::ChannelAll<ProgressInformation>>(
          [](ProgressInformation) {});
};

// Returns all matches starting at start. If end is not nullptr, only matches
// in the region enclosed by start and *end will be returned.
language::ValueOrError<std::vector<language::text::LineColumn>> SearchHandler(
    Direction direction, const SearchOptions& options,
    const language::text::LineSequence& buffer);

void HandleSearchResults(
    const language::ValueOrError<std::vector<language::text::LineColumn>>&
        results_or_error,
    OpenBuffer& buffer);

language::ValueOrError<language::text::LineColumn> GetNextMatch(
    Direction direction, const SearchOptions& options,
    const language::text::LineSequence& contents);

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
}  // namespace editor
}  // namespace afc

#endif
