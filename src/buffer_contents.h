#ifndef __AFC_EDITOR_BUFFER_CONTENTS_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_H__

#include <memory>
#include <vector>

#include "src/cursors.h"
#include "src/line.h"
#include "src/line_column.h"
#include "src/tree.h"

namespace afc {
namespace editor {

using std::string;
using std::unique_ptr;
using std::vector;

class BufferContents {
 public:
  BufferContents() = default;

  wint_t character_at(const LineColumn& position) const;

  LineNumberDelta size() const { return LineNumberDelta(lines_.size()); }

  LineNumber EndLine() const;

  // Returns a copy of the contents of the tree. Complexity is linear to the
  // number of lines (the lines themselves aren't actually copied).
  std::unique_ptr<BufferContents> copy() const;

  shared_ptr<const Line> at(LineNumber position) const {
    CHECK_LT(position, LineNumber(lines_.size()));
    return lines_.at(position.line);
  }

  shared_ptr<const Line> back() const {
    CHECK(!lines_.empty());
    return at(EndLine());
  }

  shared_ptr<const Line> front() const {
    CHECK(!lines_.empty());
    return lines_.at(0);
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
    auto it = std::upper_bound(lines_.begin(), lines_.end(), key, compare);
    return LineNumber(distance(lines_.begin(), it));
  }

  size_t CountCharacters() const;

  void insert_line(LineNumber line_position, shared_ptr<const Line> line);

  // Does not call NotifyUpdateListeners! That should be done by the caller.
  // Avoid calling this in general: prefer calling the other functions (that
  // have more semantic information about what you're doing).
  void set_line(LineNumber position, shared_ptr<const Line> line);

  template <class C>
  void sort(LineNumber first, LineNumber last, C compare) {
    std::sort(lines_.begin() + first.line, lines_.begin() + last.line, compare);
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

  // Sets the character and modifiers in line `line` and column `column`.
  //
  // `line` must be smaller than size().
  //
  // `column` may be greater than size() of the current line, in which case the
  // character will just get appended (extending the line by exactly one
  // character).
  //
  // TODO: Use LineColumn?
  void SetCharacter(LineNumber line, ColumnNumber column, int c,
                    std::unordered_set<LineModifier, hash<int>> modifiers);

  // TODO: Use LineColumn?
  void InsertCharacter(LineNumber line, ColumnNumber column);
  void AppendToLine(LineNumber line, const Line& line_to_append);

  enum class CursorsBehavior { kAdjust, kUnmodified };

  void EraseLines(LineNumber first, LineNumber last,
                  CursorsBehavior cursors_behavior);

  void SplitLine(LineColumn position);

  // Appends the next line to the current line and removes the next line.
  // Essentially, removes the \n at the end of the current line.
  void FoldNextLine(LineNumber line);

  void push_back(wstring str);
  void push_back(shared_ptr<const Line> line) {
    lines_.push_back(line);
    NotifyUpdateListeners(CursorsTracker::Transformation());
  }

  void AddUpdateListener(
      std::function<void(const CursorsTracker::Transformation&)> listener);

 private:
  void NotifyUpdateListeners(
      const CursorsTracker::Transformation& cursor_adjuster);

  Tree<shared_ptr<const Line>> lines_ = []() {
    Tree<shared_ptr<const Line>> output;
    output.insert(output.begin(), std::make_shared<Line>());
    return output;
  }();

  vector<std::function<void(const CursorsTracker::Transformation&)>>
      update_listeners_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_CONTENTS_H__
