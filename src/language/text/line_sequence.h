#ifndef __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
#define __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__

#include <memory>
#include <vector>

#include "src/language/const_tree.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
// TODO(trivial): Once we've moved customers accordingly, get rid of this
// include. Customers of MutableLineSequence should include it directly.
#include "src/language/text/mutable_line_sequence.h"
#include "src/tests/fuzz_testable.h"

namespace afc::language::text {
// TODO: Add more methods here.
class MutableLineSequence;
class LineSequence {
 private:
  using Lines = language::ConstTree<
      language::VectorBlock<
          language::NonNull<std::shared_ptr<const language::text::Line>>, 256>,
      256>;

 public:
 private:
  friend MutableLineSequence;
  Lines::Ptr lines_ = Lines::PushBack(nullptr, {});
};
}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
