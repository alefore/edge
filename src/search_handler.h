#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "src/async_processor.h"
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

class AsyncSearchProcessor {
 public:
  AsyncSearchProcessor(
      std::shared_ptr<WorkQueue> work_queue,
      BackgroundCallbackRunner::Options::QueueBehavior queue_behavior =
          BackgroundCallbackRunner::Options::QueueBehavior::kFlush);

  struct Output {
    std::optional<std::wstring> pattern_error;
    int matches = 0;
    enum class SearchCompletion {
      // The search was interrupted. It's possible that there are more matches.
      kInterrupted,
      // The search consumed the entire input.
      kFull,
      // The pattern was invalid (see pattern_error for details).
      kInvalidPattern,
    };
    SearchCompletion search_completion = SearchCompletion::kFull;

    std::wstring ToString() const;
  };

  futures::Value<Output> Search(SearchOptions search_options,
                                const OpenBuffer& buffer,
                                std::shared_ptr<ProgressChannel> channel);

 private:
  AsyncEvaluator evaluator_;
};

}  // namespace editor
}  // namespace afc

#endif
