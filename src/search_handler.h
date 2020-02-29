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

struct SearchOptions {
  // The buffer in which to search.
  OpenBuffer* buffer;

  // The position in which to start searching for positions.
  LineColumn starting_position;

  // The regular expression to search.
  wstring search_query;

  // TODO(easy): Get rid of this? Have search just take it from the buffer's
  // variable?
  bool case_sensitive = false;

  // An optional position where the search should stop.
  std::optional<LineColumn> limit_position;

  // If set, signals that it is okay for the search operation to stop once this
  // number of positions has been found.
  std::optional<size_t> required_positions;

  // When notified, interrupts the search. Must not be nullptr.
  std::shared_ptr<Notification> abort_notification =
      std::make_shared<Notification>();
};

std::vector<LineColumn> SearchHandler(EditorState* editor_state,
                                      const SearchOptions& options);

void JumpToNextMatch(EditorState* editor_state, const SearchOptions& options);

class AsyncSearchProcessor {
 public:
  AsyncSearchProcessor(WorkQueue* work_queue);

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
                                std::unique_ptr<BufferContents> buffer,
                                std::shared_ptr<ProgressChannel> channel);

 private:
  AsyncEvaluator evaluator_;
};

}  // namespace editor
}  // namespace afc

#endif
