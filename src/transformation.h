#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <list>
#include <memory>

#include <glog/logging.h>

#include "direction.h"
#include "modifiers.h"
#include "structure.h"

namespace afc {
namespace editor {

using std::list;
using std::unique_ptr;
using std::shared_ptr;

class EditorState;
class OpenBuffer;
class LineColumn;

class TransformationStack;

class Transformation {
 public:
  struct Result {
    Result(EditorState* editor_state);

    // Did the transformation run to completion?  If it only run partially, this
    // should be false.
    bool success;

    // Did the transformation actually make any progress?  Some transformations
    // succeed without actually having any effect; we use this to stop iterating
    // them needlessly.
    bool made_progress;

    // This the transformation made any actual changes to the contents of the
    // buffer?
    bool modified_buffer;

    // Transformation that will undo any changes done by this one.
    unique_ptr<TransformationStack> undo_stack;

    // Any text deleted will be appended to this buffer.  If any text at all is
    // appended, the buffer will replace the previous paste buffer.
    shared_ptr<OpenBuffer> delete_buffer;

    // Input and ouput parameter: where should the transformation be applied and
    // where does the cursor end up afterwards.
    LineColumn cursor;
  };

  virtual ~Transformation() {}
  virtual void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const = 0;
  // TODO: Add const qualifier.
  virtual unique_ptr<Transformation> Clone() = 0;
};

enum InsertBufferTransformationPosition {
  // Leaves the buffer position at the start of the inserted text.
  START,

  // Leaves the buffer position at the end of the inserted text.
  END,
};

unique_ptr<Transformation> NewInsertBufferTransformation(
    shared_ptr<const OpenBuffer> buffer_to_insert, size_t repetitions,
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
unique_ptr<Transformation> NewSetRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation);

// Returns a transformation that repeats another transformation a given number
// of times.
unique_ptr<Transformation> NewApplyRepetitionsTransformation(
    size_t repetitions, unique_ptr<Transformation> transformation);

unique_ptr<Transformation> NewDirectionTransformation(
    Direction direction, unique_ptr<Transformation> transformation);

unique_ptr<Transformation> NewStructureTransformation(
    Structure structure,
    Modifiers::StructureRange structure_modifier,
    unique_ptr<Transformation> transformation);

class TransformationStack : public Transformation {
 public:
  void PushBack(unique_ptr<Transformation> transformation) {
    stack_.push_back(std::move(transformation));
  }

  void PushFront(unique_ptr<Transformation> transformation) {
    stack_.push_front(std::move(transformation));
  }

  void Apply(EditorState* editor_state, OpenBuffer* buffer, Result* result)
      const override {
    CHECK(result != nullptr);
    for (auto& it : stack_) {
      Result it_result(editor_state);
      it_result.delete_buffer = result->delete_buffer;
      it_result.cursor = result->cursor;
      it->Apply(editor_state, buffer, &it_result);
      result->cursor = it_result.cursor;
      if (it_result.modified_buffer) {
        result->modified_buffer = true;
      }
      if (it_result.made_progress) {
        result->made_progress = true;
      }
      result->undo_stack->PushFront(std::move(it_result.undo_stack));
      if (!it_result.success) {
        result->success = false;
        break;
      }
    }
  }

  unique_ptr<Transformation> Clone() {
    unique_ptr<TransformationStack> output(new TransformationStack());
    for (auto& it : stack_) {
      output->PushBack(it->Clone());
    }
    return std::move(output);
  }

 private:
  list<unique_ptr<Transformation>> stack_;
};

}  // namespace editor
}  // namespace afc

#endif
