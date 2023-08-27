#ifndef __AFC_EDITOR_TRANSFORMATION_RESULT_H__
#define __AFC_EDITOR_TRANSFORMATION_RESULT_H__

#include <memory>

#include "src/language/text/line_column.h"

namespace afc::editor {
class OpenBuffer;
namespace transformation {
class Stack;
struct Result {
  Result(language::text::LineColumn input_position);
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
  language::NonNull<std::unique_ptr<transformation::Stack>> undo_stack;

  bool added_to_paste_buffer = false;

  // Where should the cursor move to after the transformation?
  language::text::LineColumn position;
};
}  // namespace transformation
}  // namespace afc::editor
#endif  //__AFC_EDITOR_TRANSFORMATION_RESULT_H__
