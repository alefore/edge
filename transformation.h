#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <memory>

namespace afc {
namespace editor {

using std::unique_ptr;
using std::shared_ptr;

class EditorState;
class OpenBuffer;
class LineColumn;

class Transformation {
 public:
  virtual ~Transformation() {}
  virtual unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const = 0;
};

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<OpenBuffer> buffer_to_insert,
    const LineColumn& position,
    size_t repetitions);

unique_ptr<Transformation> NewDeleteTransformation(
    const LineColumn& start, const LineColumn& end);

}  // namespace editor
}  // namespace afc

#endif
