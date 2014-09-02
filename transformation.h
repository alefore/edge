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

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<OpenBuffer> buffer_to_insert,
    const LineColumn& position,
    size_t repetitions);

unique_ptr<Transformation> NewDeleteTransformation(
    const LineColumn& start, const LineColumn& end);

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
