#ifndef __AFC_EDITOR_TRANSFORMATION_RESULT_H__
#define __AFC_EDITOR_TRANSFORMATION_RESULT_H__

#include <memory>

namespace afc::editor {
class OpenBuffer;
namespace transformation {
class Stack;
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
  std::unique_ptr<transformation::Stack> undo_stack;

  // If set to a buffer, it will replace the previous paste buffer.
  std::shared_ptr<OpenBuffer> delete_buffer;

  // Where should the cursor move to after the transformation?
  LineColumn position;
};
}  // namespace transformation
}  // namespace afc::editor
#endif  //__AFC_EDITOR_TRANSFORMATION_RESULT_H__
