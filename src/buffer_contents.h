#ifndef __AFC_EDITOR_BUFFER_CONTENTS_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_H__

#include <memory>
#include <vector>

#include "src/const_tree.h"
#include "src/cursors.h"
#include "src/fuzz_testable.h"
#include "src/infrastructure/tracker.h"
#include "src/language/safe_types.h"
#include "src/line.h"
#include "src/line_column.h"

namespace afc {
namespace editor {

class BufferContents : public fuzz::FuzzTestable {
  using Lines = ConstTree<language::NonNull<std::shared_ptr<const Line>>>;

 public:
  using UpdateListener =
      std::function<void(const CursorsTracker::Transformation&)>;

  BufferContents();
  virtual ~BufferContents() = default;

  explicit BufferContents(language::NonNull<std::shared_ptr<const Line>> line);
  explicit BufferContents(UpdateListener update_listener);

  wint_t character_at(const LineColumn& position) const;

  LineColumn PositionBefore(LineColumn position) const;
  LineColumn PositionAfter(LineColumn position) const;

  LineNumberDelta size() const { return LineNumberDelta(Lines::Size(lines_)); }

  // The last valid line (which can be fed to `at`).
  LineNumber EndLine() const;
  Range range() const;

  // Returns a copy of the contents of the tree. No actual copying takes place.
  // This is dirt cheap. The updates listener isn't copied.
  language::NonNull<std::unique_ptr<BufferContents>> copy() const;

  // Drops all contents outside of a specific range.
  void FilterToRange(Range range);

  language::NonNull<std::shared_ptr<const Line>> at(
      LineNumber line_number) const {
    CHECK_LT(line_number, LineNumber(0) + size());
    return lines_->Get(line_number.read());
  }

  language::NonNull<std::shared_ptr<const Line>> back() const {
    CHECK(lines_ != nullptr);
    return at(EndLine());
  }

  language::NonNull<std::shared_ptr<const Line>> front() const {
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
  void ForEach(const std::function<void(std::wstring)>& callback) const;

  std::wstring ToString() const;

  template <class C>
  LineNumber upper_bound(language::NonNull<std::shared_ptr<const Line>>& key,
                         C compare) const {
    return LineNumber(Lines::UpperBound(lines_, key, compare));
  }

  size_t CountCharacters() const;

  void insert_line(LineNumber line_position,
                   language::NonNull<std::shared_ptr<const Line>> line);

  // Does not call update_listener_! That should be done by the caller. Avoid
  // calling this in general: prefer calling the other functions (that have more
  // semantic information about what you're doing).
  void set_line(LineNumber position,
                language::NonNull<std::shared_ptr<const Line>> line);

  template <class C>
  void sort(LineNumber first, LineNumber last, C compare) {
    // TODO: Only append to `lines` the actual range [first, last), and then
    // just Append to prefix/suffix.
    std::vector<language::NonNull<std::shared_ptr<const Line>>> lines;
    Lines::Every(lines_,
                 [&lines](language::NonNull<std::shared_ptr<const Line>> line) {
                   lines.push_back(line);
                   return true;
                 });
    std::sort(lines.begin() + first.read(), lines.begin() + last.read(),
              compare);
    lines_ = nullptr;
    for (auto& line : lines) {
      lines_ = Lines::PushBack(std::move(lines_), std::move(line));
    }
    update_listener_(CursorsTracker::Transformation());
  }

  // If modifiers is present, applies it to every character (overriding
  // modifiers from the source).
  void insert(LineNumber position_line, const BufferContents& source,
              const std::optional<LineModifierSet>& modifiers);

  // Delete characters from position.line in range [position.column,
  // position.column + amount). Amount must not be negative and it must be in a
  // valid range.
  void DeleteCharactersFromLine(LineColumn position, ColumnNumberDelta amount);
  // Delete characters from position.line in range [position.column, ...).
  void DeleteToLineEnd(LineColumn position);

  // Sets the character and modifiers in a given position.
  //
  // `position.line` must be smaller than size().
  //
  // `position.column` may be greater than size() of the current line, in which
  // case the character will just get appended (extending the line by exactly
  // one character).
  void SetCharacter(LineColumn position, int c,
                    std::unordered_set<LineModifier, std::hash<int>> modifiers);

  void InsertCharacter(LineColumn position);
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

  void push_back(std::wstring str);
  void push_back(language::NonNull<std::shared_ptr<const Line>> line);
  void append_back(
      std::vector<language::NonNull<std::shared_ptr<const Line>>> lines);

  void SetUpdateListener(UpdateListener update_listener);

  std::vector<fuzz::Handler> FuzzHandlers() override;

 private:
  template <typename Callback>
  void TransformLine(LineNumber line_number, Callback callback) {
    TransformLine(line_number, std::move(callback),
                  CursorsTracker::Transformation());
  }

  template <typename Callback>
  void TransformLine(LineNumber line_number, Callback callback,
                     CursorsTracker::Transformation cursors_transformation) {
    static infrastructure::Tracker tracker(L"BufferContents::TransformLine");
    auto tracker_call = tracker.Call();
    if (lines_ == nullptr) {
      lines_ = Lines::PushBack(nullptr, {});
    }
    CHECK_LE(line_number, EndLine());
    Line::Options options = at(line_number)->CopyOptions();
    callback(options);
    set_line(line_number,
             language::MakeNonNullShared<const Line>(std::move(options)));
    update_listener_(cursors_transformation);
  }

  Lines::Ptr lines_ = Lines::PushBack(nullptr, {});

  UpdateListener update_listener_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_CONTENTS_H__
