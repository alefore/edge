#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <list>
#include <memory>

#include <glog/logging.h>

#include "direction.h"

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
  struct Result {
    Result();

    // Did the transformation run to completion?  If it only run partially, this
    // should be false.
    bool success;

    // This the transformation made any actual changes to the contents of the
    // buffer?
    bool modified_buffer;

    // Reverse transformation that will undo any changes done by this one.  This
    // should never be null (see NewNoopTransformation instead).
    unique_ptr<Transformation> undo;
  };

  virtual ~Transformation() {}
  virtual void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const = 0;
  virtual unique_ptr<Transformation> Clone() = 0;
  virtual bool ModifiesBuffer() = 0;
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

unique_ptr<Transformation> NewGotoPositionTransformation(
    const LineColumn& position);

unique_ptr<Transformation> NewNoopTransformation();

// Goes to a given position and applies a transformation.
unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation);

unique_ptr<Transformation> ComposeTransformation(
    unique_ptr<Transformation> a, unique_ptr<Transformation> b);

// Returns a transformation that deletes superfluous characters (based on
// OpenBuffer::variable_line_suffix_superfluous_characters) from the current
// line.
unique_ptr<Transformation> NewDeleteSuffixSuperfluousCharacters();

// Returns a transformation that sets the number of repetitions to a given
// value, calls a delegate and then restores the original number.
unique_ptr<Transformation> NewRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation);

class TransformationStack : public Transformation {
 public:
  void PushBack(unique_ptr<Transformation> transformation) {
    stack_.push_back(std::move(transformation));
  }

  void PushFront(unique_ptr<Transformation> transformation) {
    stack_.push_front(std::move(transformation));
  }

  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    CHECK(result != nullptr);
    unique_ptr<TransformationStack> undo(new TransformationStack());
    for (auto& it : stack_) {
      Result it_result;
      it->Apply(editor_state, buffer, &it_result);
      result->modified_buffer |= it_result.modified_buffer;
      undo->PushFront(std::move(it_result.undo));
      if (!it_result.success) {
        result->success = false;
        break;
      }
    }
    result->undo = std::move(undo);
  }

  unique_ptr<Transformation> Clone() {
    unique_ptr<TransformationStack> output(new TransformationStack());
    for (auto& it : stack_) {
      output->PushBack(it->Clone());
    }
    return std::move(output);
  }

  virtual bool ModifiesBuffer() {
    for (auto& it : stack_) {
      if (it->ModifiesBuffer()) { return true; }
    }
    return false;
  }

 private:
  list<unique_ptr<Transformation>> stack_;
};

}  // namespace editor
}  // namespace afc

#endif
