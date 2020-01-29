#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "src/async_processor.h"
#include "src/buffer_contents.h"
#include "src/futures/futures.h"
#include "src/line_column.h"
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
};

std::vector<LineColumn> SearchHandler(EditorState* editor_state,
                                      const SearchOptions& options);

void JumpToNextMatch(EditorState* editor_state, const SearchOptions& options);

class AsyncSearchProcessor {
 public:
  AsyncSearchProcessor(WorkQueue* work_queue);

  struct Output {
    enum class Results { kInvalidPattern, kNoMatches, kOneMatch, kManyMatches };
    Results results;
  };

  futures::Value<Output> Search(SearchOptions search_options,
                                std::unique_ptr<BufferContents> buffer);

 private:
  AsyncEvaluator evaluator_;
};

}  // namespace editor
}  // namespace afc

#endif
