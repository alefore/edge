#ifndef __AFC_EDITOR_PREDICTOR_H__
#define __AFC_EDITOR_PREDICTOR_H__

#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace editor {

using std::function;
using std::shared_ptr;
using std::wstring;
using std::vector;

class EditorState;
class OpenBuffer;

typedef function<void(EditorState*, const wstring&, OpenBuffer*)> Predictor;

shared_ptr<OpenBuffer> PredictionsBuffer(
    EditorState* editor_state,
    Predictor predictor,
    const wstring& input,
    function<void(const wstring&)> consumer);

void FilePredictor(EditorState* editor_state,
                   const wstring& input,
                   OpenBuffer* buffer);

void EmptyPredictor(EditorState* editor_state,
                    const wstring& input,
                    OpenBuffer* buffer);

Predictor PrecomputedPredictor(const vector<wstring>& predictions);

}  // namespace editor
}  // namespace afc

#endif
