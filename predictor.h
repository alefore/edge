#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <memory>

#include "editor.h"

namespace afc {
namespace editor {

using std::function;

typedef function<void(EditorState* editor_state, const string& input, shared_ptr<OpenBuffer>)> Predictor;

void FilePredictor(EditorState* editor_state,
                   const string& input,
                   shared_ptr<OpenBuffer> buffer);

void EmptyPredictor(EditorState* editor_state,
                   const string& input,
                   shared_ptr<OpenBuffer> buffer);

}  // namespace editor
}  // namespace afc

#endif
