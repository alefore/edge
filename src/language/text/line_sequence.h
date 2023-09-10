#ifndef __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
#define __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__

#include <memory>
#include <vector>

#include "src/language/const_tree.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/tests/fuzz_testable.h"

namespace afc::language::text {
class MutableLineSequence;
// TODO: Add more methods here.

// This class is deeply immutable, therefore thread-safe.
class LineSequence {
 private:
  using Lines = language::ConstTree<
      language::VectorBlock<language::NonNull<std::shared_ptr<const Line>>,
                            256>,
      256>;

 public:
  LineSequence() = default;
  LineSequence(const LineSequence&) = default;

  std::wstring ToString() const;

  LineNumberDelta size() const;
  // The last valid line (which can be fed to `at`).
  LineNumber EndLine() const;
  Range range() const;

  size_t CountCharacters() const;

  language::NonNull<std::shared_ptr<const Line>> at(
      LineNumber line_number) const;
  language::NonNull<std::shared_ptr<const Line>> back() const;
  language::NonNull<std::shared_ptr<const Line>> front() const;

  // Iterates: runs the callback on every line in the buffer, passing as the
  // first argument the line count (starts counting at 0). Stops the iteration
  // if the callback returns false. Returns true iff the callback always
  // returned true.
  bool EveryLine(
      const std::function<bool(LineNumber, const Line&)>& callback) const;

  // Convenience wrappers of the above.
  void ForEach(
      const std::function<void(const language::text::Line&)>& callback) const;
  void ForEach(const std::function<void(std::wstring)>& callback) const;

  wint_t character_at(const LineColumn& position) const;

  LineColumn AdjustLineColumn(LineColumn position) const;

 private:
  friend MutableLineSequence;
  LineSequence(Lines::Ptr lines) : lines_(lines) {}

  Lines::Ptr lines_ = Lines::PushBack(nullptr, {});
};
}  // namespace afc::language::text

// TODO(trivial): Once we've moved customers accordingly, get rid of this
// include. Customers of MutableLineSequence should include it directly.
#include "src/language/text/mutable_line_sequence.h"

#endif  // __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
