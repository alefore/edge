#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/futures/futures.h"
#include "src/status.h"
#include "src/structure.h"

namespace afc {
namespace editor {

using std::function;
using std::shared_ptr;
using std::vector;
using std::wstring;

class EditorState;
class OpenBuffer;

// A Predictor is a function that generates predictions (autocompletions) for a
// given prompt input and writes them to a buffer.
struct PredictorInput {
  EditorState* editor = nullptr;
  std::wstring input;

  // The output buffer to write predictions to.
  //
  // It begins with nothing but an empty line. As such, you probably don't want
  // to use OpenBuffer::AppendLine (because that would leave an empty line at
  // the beggining) but, instead, OpenBuffer::AppendToLastLine, followed by
  // OpenBuffer::AppendRawLine(..., empty_line). In other words, once the
  // predictor is done running, the buffer must have an empty line at the end
  // (and not at the beginning).
  OpenBuffer* predictions = nullptr;

  // If the completion is specific to a given buffer (as opposed to in a global
  // status, with no corresponding buffer), this will be pointing to the buffer.
  // That allows the predictor to inspect the buffer contents (e.g., searching
  // in the buffer) or variables (e.g., honoring variables in the buffer
  // affecting the prediction).
  std::shared_ptr<OpenBuffer> source_buffer = nullptr;
};

struct PredictorOutput {};
using Predictor =
    std::function<futures::DelayedValue<PredictorOutput>(PredictorInput)>;

const wstring& PredictionsBufferName();

struct PredictResults {
  // If the input matched at least one item, this will be the longest common
  // prefix of all the items that the input matched.
  std::optional<wstring> common_prefix;

  // The buffer holding all the predictions.
  std::shared_ptr<OpenBuffer> predictions_buffer;

  int matches = 0;

  // The size of the longest prefix in the input that matched at least one item.
  // Typically, when the input matches at least one item, this will be the size
  // of the input.
  ColumnNumberDelta longest_prefix;
  // The size of the longest prefix in the input that matched a directory and
  // that is shorter than the entire input (i.e., if the input is `foo/bar` and
  // that directory exists, the longest directory will be `foo`).
  ColumnNumberDelta longest_directory_match;

  // Did the input match a file exactly?
  bool found_exact_match = false;
};

std::ostream& operator<<(std::ostream& os, const PredictResults& lc);

struct PredictOptions {
  EditorState* editor_state;
  Predictor predictor;
  Status* status;

  // The buffer that contains the input to use for the prediction.
  std::shared_ptr<OpenBuffer> input_buffer;

  Structure* input_selection_structure = StructureLine();

  // Given to the predictor (see `PredictorInput::source_buffer`).
  std::shared_ptr<OpenBuffer> source_buffer;

  // Called if all the predicted entries have a common prefix that's longer than
  // the query.
  std::function<void(PredictResults)> callback;
};
// Create a new buffer running a given predictor on the input in a given status
// prompt. When that's done, runs consumer on the results (on the longest
// unambiguous completion for input).
void Predict(PredictOptions predict_options);

futures::DelayedValue<PredictorOutput> FilePredictor(PredictorInput input);

futures::DelayedValue<PredictorOutput> EmptyPredictor(PredictorInput input);

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator);

Predictor DictionaryPredictor(std::shared_ptr<OpenBuffer> dictionary);

// Buffer must be a buffer given to a predictor by `Predict`. Registers a new
// size of a prefix that has a match.
void RegisterPredictorPrefixMatch(size_t longest_prefix, OpenBuffer* buffer);
void RegisterPredictorDirectoryMatch(size_t prefix, OpenBuffer* buffer);
void RegisterPredictorExactMatch(OpenBuffer* buffer);

}  // namespace editor
}  // namespace afc

#endif
