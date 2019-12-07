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
typedef function<void(EditorState*, const wstring&, OpenBuffer*)> Predictor;

const wstring& PredictionsBufferName();

// Create a new buffer running a given predictor on the input in a given status
// prompt. When that's done, runs consumer on the results (on the longest
// unambiguous completion for input).
void Predict(EditorState* editor_state, Predictor predictor, Status* status,
             function<void(const wstring&)> consumer);

void FilePredictor(EditorState* editor_state, const wstring& input,
                   OpenBuffer* buffer);

void EmptyPredictor(EditorState* editor_state, const wstring& input,
                    OpenBuffer* buffer);

Predictor PrecomputedPredictor(const vector<wstring>& predictions,
                               wchar_t separator);

}  // namespace editor
}  // namespace afc

#endif
