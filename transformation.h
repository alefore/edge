#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <list>
#include <memory>

namespace afc {
namespace editor {

using std::list;
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

enum InsertBufferTransformationPosition {
  // Leaves the buffer position at the start of the inserted text.
  START,

  // Leaves the buffer position at the end of the inserted text.
  END,
};

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<OpenBuffer> buffer_to_insert, size_t repetitions,
    InsertBufferTransformationPosition insert_buffer_transformation_position);

unique_ptr<Transformation> NewDeleteCharactersTransformation(
    size_t repetitions, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteWordsTransformation(
    size_t repetitions, bool copy_to_paste_buffer);
unique_ptr<Transformation> NewDeleteLinesTransformation(
    size_t repetitions, bool copy_to_paste_buffer);

// DEPRECATED: Use one of the above transformations, which are more semantically
// reach (and thus better for repeating).
unique_ptr<Transformation> NewDeleteTransformation(
    const LineColumn& start, const LineColumn& end, bool copy_to_paste_buffer);

unique_ptr<Transformation> NewNoopTransformation();

// Goes to a given position and applies a transformation.
unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation);

unique_ptr<Transformation> ComposeTransformation(
    unique_ptr<Transformation> a, unique_ptr<Transformation> b);

class TransformationStack : public Transformation {
 public:
  void PushBack(unique_ptr<Transformation> transformation) {
    stack_.push_back(std::move(transformation));
  }

  void PushFront(unique_ptr<Transformation> transformation) {
    stack_.push_front(std::move(transformation));
  }

  unique_ptr<Transformation> Apply(
      EditorState* editor_state, OpenBuffer* buffer) const {
    unique_ptr<TransformationStack> undo(new TransformationStack());
    for (auto& it : stack_) {
      undo->PushFront(it->Apply(editor_state, buffer));
    }
    return std::move(undo);
  }

 private:
  list<unique_ptr<Transformation>> stack_;
};

}  // namespace editor
}  // namespace afc

#endif
