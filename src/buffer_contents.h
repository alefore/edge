#ifndef __AFC_EDITOR_BUFFER_CONTENTS_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_H__

#include <memory>
#include <vector>

#include "src/const_tree.h"
#include "src/cursors.h"
#include "src/fuzz_testable.h"
#include "src/line.h"
#include "src/line_column.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;
using std::vector;

class BufferContents : public fuzz::FuzzTestable {
  using Lines = ConstTree<std::shared_ptr<const Line>>;

 public:
  BufferContents() = default;

  wint_t character_at(const LineColumn& position) const;

  LineNumberDelta size() const { return LineNumberDelta(Lines::Size(lines_)); }

  LineNumber EndLine() const;
  Range range() const;

  // Returns a copy of the contents of the tree. No actual copying takes place.
  std::unique_ptr<BufferContents> copy() const;

  shared_ptr<const Line> at(LineNumber position) const {
    CHECK_LT(position, LineNumber(0) + size());
    return lines_->Get(position.line);
  }

  shared_ptr<const Line> back() const {
    CHECK(lines_ != nullptr);
    return at(EndLine());
  }

  shared_ptr<const Line> front() const {
    CHECK(lines_ != nullptr);
    return at(LineNumber(0));
  }

  // Iterates: runs the callback on every line in the buffer, passing as the
  // first argument the line count (starts counting at 0). Stops the iteration
  // if the callback returns false. Returns true iff the callback always
  // returned true.
  bool EveryLine(
      const std::function<bool(LineNumber, const Line&)>& callback) const;

  // Convenience wrappers of the above.
  void ForEach(const std::function<void(const Line&)>& callback) const;
  void ForEach(const std::function<void(wstring)>& callback) const;

  wstring ToString() const;

  template <class C>
  LineNumber upper_bound(std::shared_ptr<const Line>& key, C compare) const {
    return LineNumber(Lines::UpperBound(lines_, key, compare));
  }

  size_t CountCharacters() const;

  void insert_line(LineNumber line_position, shared_ptr<const Line> line);

  // Does not call NotifyUpdateListeners! That should be done by the caller.
  // Avoid calling this in general: prefer calling the other functions (that
  // have more semantic information about what you're doing).
  void set_line(LineNumber position, shared_ptr<const Line> line);

  template <class C>
  void sort(LineNumber first, LineNumber last, C compare) {
    // TODO: Only append to `lines` the actual range [first, last), and then
    // just Append to prefix/suffix.
    std::vector<std::shared_ptr<const Line>> lines;
    Lines::Every(lines_, [&lines](std::shared_ptr<const Line> line) {
      lines.push_back(line);
      return true;
    });
    std::sort(lines.begin() + first.line, lines.begin() + last.line, compare);
    lines_ = nullptr;
    for (auto& line : lines) {
      lines_ = Lines::PushBack(std::move(lines_), std::move(line));
    }
    NotifyUpdateListeners(CursorsTracker::Transformation());
  }

  void insert(LineNumber position_line, const BufferContents& source,
              const LineModifierSet* modifiers);

  // Delete characters from the given line in range [column, column + amount).
  // Amount must not be negative.
  //
  // TODO: Use LineColumn?
  void DeleteCharactersFromLine(LineNumber line, ColumnNumber column,
                                ColumnNumberDelta amount);
  // Delete characters from the given line in range [column, ...).
  //
  // TODO: Use LineColumn?
  void DeleteCharactersFromLine(LineNumber line, ColumnNumber column);

  // Sets the character and modifiers in a given position.
  //
  // `position.line` must be smaller than size().
  //
  // `position.column` may be greater than size() of the current line, in which
  // case the character will just get appended (extending the line by exactly
  // one character).
  void SetCharacter(LineColumn position, int c,
                    std::unordered_set<LineModifier, hash<int>> modifiers);

  // TODO: Use LineColumn?
  void InsertCharacter(LineNumber line, ColumnNumber column);
  void AppendToLine(LineNumber line, Line line_to_append);

  enum class CursorsBehavior { kAdjust, kUnmodified };

  void EraseLines(LineNumber first, LineNumber last,
                  CursorsBehavior cursors_behavior);

  void SplitLine(LineColumn position);

  // Appends the next line to the current line and removes the next line.
  // Essentially, removes the \n at the end of the current line.
  //
  // If the line is out of range, doesn't do anything.
  void FoldNextLine(LineNumber line);

  void push_back(wstring str);
  void push_back(shared_ptr<const Line> line) {
    lines_ = Lines::PushBack(std::move(lines_), line);
    NotifyUpdateListeners(CursorsTracker::Transformation());
  }

  void AddUpdateListener(
      std::function<void(const CursorsTracker::Transformation&)> listener);

  std::vector<fuzz::Handler> FuzzHandlers() override;

 private:
  void NotifyUpdateListeners(
      const CursorsTracker::Transformation& cursor_adjuster);

  Lines::Ptr lines_ = Lines::PushBack(nullptr, std::make_shared<Line>());

  vector<std::function<void(const CursorsTracker::Transformation&)>>
      update_listeners_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_CONTENTS_H__
