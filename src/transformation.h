#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <glog/logging.h>

#include <list>
#include <memory>

#include "src/direction.h"
#include "src/line.h"
#include "src/modifiers.h"
#include "src/structure.h"
#include "src/transformation/noop.h"

namespace afc {
namespace editor {

using std::list;
using std::shared_ptr;
using std::unique_ptr;

class EditorState;
class OpenBuffer;
struct LineColumn;

class TransformationStack;

class Transformation {
 public:
  struct Result {
    Result(OpenBuffer* buffer);

    // The buffer that the transformation should modify.
    OpenBuffer* buffer = nullptr;

    // Input parameter.
    enum class Mode {
      // Just preview what this transformation would do. Don't apply any
      // long-lasting effects.
      kPreview,
      // Apply the transformation.
      kFinal,
    };
    // Input parameter.
    Mode mode = Mode::kFinal;

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
    std::unique_ptr<TransformationStack> undo_stack;

    // Any text deleted will be appended to this buffer.  If any text at all is
    // appended, the buffer will replace the previous paste buffer.
    shared_ptr<OpenBuffer> delete_buffer;

    // Input and ouput parameter: where should the transformation be applied and
    // where does the cursor end up afterwards.
    LineColumn cursor;
  };

  virtual ~Transformation() {}
  virtual void Apply(Result* result) const = 0;
  virtual unique_ptr<Transformation> Clone() const = 0;
};

struct InsertOptions {
  std::shared_ptr<const OpenBuffer> buffer_to_insert;

  Modifiers modifiers;

  enum class FinalPosition {
    // Leaves the buffer position at the start of the inserted text.
    kStart,

    // Leaves the buffer position at the end of the inserted text.
    kEnd,
  };
  // Ignored if `position` is set.
  FinalPosition final_position = FinalPosition::kEnd;

  std::optional<LineModifierSet> modifiers_set;

  // If not present, will insert wherever the cursor is. If present, inserts the
  // text at this position.
  std::optional<LineColumn> position;
};

std::unique_ptr<Transformation> NewInsertBufferTransformation(
    InsertOptions options);

// Goes to a given position and applies a transformation.
unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, unique_ptr<Transformation> transformation);

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
    Structure* structure, Modifiers::StructureRange structure_modifier,
    unique_ptr<Transformation> transformation);

class RunIfModeTransformation : public Transformation {
 public:
  RunIfModeTransformation(Transformation::Result::Mode mode,
                          std::unique_ptr<Transformation> delegate)
      : mode_(mode), delegate_(std::move(delegate)) {}

  void Apply(Result* result) const override {
    if (result->mode == mode_) {
      delegate_->Apply(result);
    }
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<RunIfModeTransformation>(mode_, delegate_->Clone());
  }

 private:
  const Transformation::Result::Mode mode_;
  const std::unique_ptr<Transformation> delegate_;
};

class TransformationWithMode : public Transformation {
 public:
  TransformationWithMode(Transformation::Result::Mode mode,
                         std::unique_ptr<Transformation> delegate)
      : mode_(mode), delegate_(std::move(delegate)) {}

  void Apply(Result* result) const override {
    auto original_mode = result->mode;
    result->mode = mode_;
    delegate_->Apply(result);
    result->mode = original_mode;
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<TransformationWithMode>(mode_, delegate_->Clone());
  }

 private:
  const Transformation::Result::Mode mode_;
  const std::unique_ptr<Transformation> delegate_;
};

}  // namespace editor
}  // namespace afc

#endif
