#ifndef __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
#define __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__

#include <memory>
#include <vector>

#include "src/language/const_tree.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/language/text/range.h"
#include "src/tests/fuzz_testable.h"

namespace afc::language::text {
class MutableLineSequence;
class SortedLineSequence;
// TODO: Add more methods here.

// This class is thread-compatible.
class LineSequence {
 private:
  using Lines = language::ConstTree<
      language::VectorBlock<language::NonNull<std::shared_ptr<const Line>>,
                            256>,
      256>;

 public:
  LineSequence() = default;
  LineSequence(const LineSequence&) = default;
  LineSequence(LineSequence&&) = default;
  LineSequence& operator=(const LineSequence&) = default;

  static LineSequence ForTests(std::vector<std::wstring> inputs);
  static LineSequence WithLine(NonNull<std::shared_ptr<Line>> line);

  // Returns a new LineSequence that contains a subset of the current one.
  LineSequence ViewRange(language::text::Range range) const;

  lazy_string::LazyString ToLazyString() const;
  std::wstring ToString() const;

  LineNumberDelta size() const;
  bool empty() const;
  // The last valid line (which can be fed to `at`).
  LineNumber EndLine() const;
  Range range() const;

  size_t CountCharacters() const;

  const language::NonNull<std::shared_ptr<const Line>>& at(
      LineNumber line_number) const;
  language::NonNull<std::shared_ptr<const Line>> back() const;
  language::NonNull<std::shared_ptr<const Line>> front() const;

  // Iterates: runs the callback on every line in the buffer intersecting the
  // range, passing as the first argument the line count (starts counting at 0).
  // Stops the iteration if the callback returns false. Returns true iff the
  // callback always returned true.
  bool ForEachLine(
      LineNumber start, LineNumberDelta length,
      const std::function<bool(
          LineNumber, const language::NonNull<std::shared_ptr<const Line>>&)>&
          callback) const;

  // Convenience wrappers of the above.
  bool EveryLine(
      const std::function<bool(
          LineNumber, const language::NonNull<std::shared_ptr<const Line>>&)>&
          callback) const;

  void ForEach(const std::function<
               void(const language::NonNull<std::shared_ptr<const Line>>&)>&
                   callback) const;
  void ForEach(const std::function<void(std::wstring)>& callback) const;

  LineSequence Map(
      const std::function<language::NonNull<std::shared_ptr<const Line>>(
          const language::NonNull<std::shared_ptr<const Line>>&)>&) const;

  wint_t character_at(const LineColumn& position) const;

  LineColumn AdjustLineColumn(LineColumn position) const;

  language::text::LineColumn PositionBefore(
      language::text::LineColumn position) const;
  language::text::LineColumn PositionAfter(
      language::text::LineColumn position) const;

 private:
  friend class MutableLineSequence;
  friend class SortedLineSequence;
  LineSequence(NonNull<Lines::Ptr> lines) : lines_(lines) {}

  NonNull<Lines::Ptr> lines_ = Lines::PushBack(nullptr, {});
};
}  // namespace afc::language::text
#endif  // __AFC_LANGUAGE_TEXT_LINE_SEQUENCE_H__
