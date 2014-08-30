#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

using std::function;

typedef function<void(EditorState* editor_state, const string& input, OpenBuffer*)> Predictor;

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
