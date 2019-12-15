#ifndef __AFC_EDITOR_SEARCH_HANDLER_H__
#define __AFC_EDITOR_SEARCH_HANDLER_H__

#include <string>
#include <vector>

#include "src/async_processor.h"
#include "src/buffer_contents.h"
#include "src/line_column.h"
#include "src/predictor.h"

namespace afc {
namespace editor {

using std::wstring;

class EditorState;
class OpenBuffer;
struct LineColumn;

void SearchHandlerPredictor(PredictorInput input);

struct SearchOptions {
  // The buffer in which to search.
  OpenBuffer* buffer;

  // The position in which to start searching for positions.
  LineColumn starting_position;

  // The regular expression to search.
  wstring search_query;

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

struct AsyncSearchOutput {
  enum class Results { kInvalidPattern, kNoMatches, kOneMatch, kManyMatches };
  Results results;
};

struct AsyncSearchInput {
  SearchOptions search_options;
  std::unique_ptr<BufferContents> buffer;
  std::function<void(const AsyncSearchOutput&)> callback;
};

std::unique_ptr<AsyncProcessor<AsyncSearchInput, AsyncSearchOutput>>
NewAsyncSearchProcessor();

}  // namespace editor
}  // namespace afc

#endif
