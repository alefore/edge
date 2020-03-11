#ifndef __AFC_EDITOR_TRANSFORMATION_H__
#define __AFC_EDITOR_TRANSFORMATION_H__

#include <glog/logging.h>

#include <memory>

#include "src/direction.h"
#include "src/futures/futures.h"
#include "src/line.h"
#include "src/modifiers.h"
#include "src/structure.h"

namespace afc::editor {
class OpenBuffer;
struct LineColumn;
class TransformationStack;

class Transformation {
 public:
  struct Input {
    explicit Input(OpenBuffer* buffer);

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

    // The buffer that the transformation should modify.
    OpenBuffer* const buffer;

    // Where should the transformation be applied?
    LineColumn position;
  };

  struct Result {
    Result(LineColumn position);
    Result(Result&&);
    ~Result();

    void MergeFrom(Result result);

    // Did the transformation run to completion?  If it only run partially, this
    // should be false.
    bool success = true;

    // Did the transformation actually make any progress?  Some transformations
    // succeed without actually having any effect; we use this to stop iterating
    // them needlessly.
    bool made_progress = false;

    // This the transformation made any actual changes to the contents of the
    // buffer?
    bool modified_buffer = false;

    // Transformation that will undo any changes done by this one.
    std::unique_ptr<TransformationStack> undo_stack;

    // If set to a buffer, it will replace the previous paste buffer.
    std::shared_ptr<OpenBuffer> delete_buffer;

    // Where should the cursor move to after the transformation?
    LineColumn position;
  };

  virtual ~Transformation() {}
  virtual futures::Value<Result> Apply(const Input& input) const = 0;
  virtual std::unique_ptr<Transformation> Clone() const = 0;
};

// Goes to a given position and applies a transformation.
std::unique_ptr<Transformation> TransformationAtPosition(
    const LineColumn& position, std::unique_ptr<Transformation> transformation);

// Returns a transformation that deletes superfluous characters (based on
// OpenBuffer::variable_line_suffix_superfluous_characters) from the current
// line.
std::unique_ptr<Transformation> NewDeleteSuffixSuperfluousCharacters();
}  // namespace afc::editor

#endif
