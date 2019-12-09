#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "src/status.h"

namespace afc {
namespace editor {

using std::function;
using std::shared_ptr;
using std::vector;
using std::wstring;

class EditorState;
class OpenBuffer;

// A function that generates predictions (autocompletions) for a given prompt
// input and writes them to a buffer.
//
// The buffer will begin with nothing but an empty line. As such, you probably
// don't want to use OpenBuffer::AppendLine (because that would leave an empty
// line at the beggining) but, instead, OpenBuffer::AppendToLastLine, followed
// by OpenBuffer::AppendRawLine(..., empty_line). In other words, once the
// predictor is done running, the buffer must have an empty line at the end (and
// not at the beginning).
//
// Once the predictor is done running, it must run the callback given.
typedef function<void(EditorState*, const wstring&, OpenBuffer*,
                      std::function<void()>)>
    Predictor;

const wstring& PredictionsBufferName();

struct PredictResults {
  // If the input matched at least one item, this will be the longest common
  // prefix of all the items that the input matched.
  std::optional<wstring> common_prefix;

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
  // Called if all the predicted entries have a common prefix that's longer than
  // the query.
  std::function<void(PredictResults)> callback;
};
// Create a new buffer running a given predictor on the input in a given status
// prompt. When that's done, runs consumer on the results (on the longest
// unambiguous completion for input).
void Predict(PredictOptions predict_options);

void FilePredictor(EditorState* editor_state, const wstring& input,
                   OpenBuffer* buffer, std::function<void()> callback);

void EmptyPredictor(EditorState* editor_state, const wstring& input,
                    OpenBuffer* buffer, std::function<void()> callback);

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator);

// Buffer must be a buffer given to a predictor by `Predict`. Registers a new
// size of a prefix that has a match.
void RegisterPredictorPrefixMatch(size_t longest_prefix, OpenBuffer* buffer);
void RegisterPredictorDirectoryMatch(size_t prefix, OpenBuffer* buffer);
void RegisterPredictorExactMatch(OpenBuffer* buffer);

}  // namespace editor
}  // namespace afc

#endif
