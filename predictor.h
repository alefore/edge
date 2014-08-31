#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <memory>
#include <vector>

namespace afc {
namespace editor {

using std::function;
using std::shared_ptr;
using std::string;
using std::vector;

class EditorState;
class OpenBuffer;

typedef function<void(EditorState*, const string&, OpenBuffer*)> Predictor;

shared_ptr<OpenBuffer> PredictionsBuffer(
    EditorState* editor_state,
    Predictor predictor,
    const string& input,
    function<void(const string&)> consumer);

void FilePredictor(EditorState* editor_state,
                   const string& input,
                   OpenBuffer* buffer);

void EmptyPredictor(EditorState* editor_state,
                   const string& input,
                   OpenBuffer* buffer);

Predictor PrecomputedPredictor(const vector<string>& predictions);

}  // namespace editor
}  // namespace afc

#endif
