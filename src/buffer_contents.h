#ifndef __AFC_EDITOR_BUFFER_CONTENTS_H__
#define __AFC_EDITOR_BUFFER_CONTENTS_H__

#include <memory>
#include <vector>

#include "src/infrastructure/screen/cursors.h"
#include "src/infrastructure/tracker.h"
#include "src/language/const_tree.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"
#include "src/tests/fuzz_testable.h"

namespace afc {
namespace editor {

class BufferContentsObserver {
 public:
  virtual ~BufferContentsObserver() = default;
  virtual void LinesInserted(language::text::LineNumber position,
                             language::text::LineNumberDelta size) = 0;
  virtual void LinesErased(language::text::LineNumber position,
                           language::text::LineNumberDelta size) = 0;
  virtual void SplitLine(language::text::LineColumn position) = 0;
  virtual void Notify(
      const infrastructure::screen::CursorsTracker::Transformation&) = 0;
};

class NullBufferContentsObserver : public BufferContentsObserver {
 public:
  void LinesInserted(language::text::LineNumber position,
                     language::text::LineNumberDelta size) override;
  void LinesErased(language::text::LineNumber position,
                   language::text::LineNumberDelta size) override;
  void SplitLine(language::text::LineColumn position) override;
  void Notify(
      const infrastructure::screen::CursorsTracker::Transformation&) override;
};

class BufferContents : public tests::fuzz::FuzzTestable {
  using Lines = language::ConstTree<
      language::VectorBlock<
          language::NonNull<std::shared_ptr<const language::text::Line>>, 256>,
      256>;

 public:
  BufferContents();

  explicit BufferContents(
      language::NonNull<std::shared_ptr<BufferContentsObserver>> observer);

  static BufferContents WithLine(
      language::NonNull<std::shared_ptr<const language::text::Line>> line);

  virtual ~BufferContents() = default;

  wint_t character_at(const language::text::LineColumn& position) const;

  language::text::LineColumn PositionBefore(
      language::text::LineColumn position) const;
  language::text::LineColumn PositionAfter(
      language::text::LineColumn position) const;

  language::text::LineNumberDelta size() const {
    return language::text::LineNumberDelta(Lines::Size(lines_));
  }

  // The last valid line (which can be fed to `at`).
  language::text::LineNumber EndLine() const;
  language::text::Range range() const;

  // Returns a copy of the contents of the tree. No actual copying takes place.
  // This is dirt cheap. The updates listener isn't copied.
  language::NonNull<std::unique_ptr<BufferContents>> copy() const;

  // Drops all contents outside of a specific range.
  void FilterToRange(language::text::Range range);

  language::NonNull<std::shared_ptr<const language::text::Line>> at(
      language::text::LineNumber line_number) const {
    CHECK_LT(line_number, language::text::LineNumber(0) + size());
    return lines_->Get(line_number.read());
  }

  language::NonNull<std::shared_ptr<const language::text::Line>> back() const {
    CHECK(lines_ != nullptr);
    return at(EndLine());
  }

  language::NonNull<std::shared_ptr<const language::text::Line>> front() const {
    CHECK(lines_ != nullptr);
    return at(language::text::LineNumber(0));
  }

  // Iterates: runs the callback on every line in the buffer, passing as the
  // first argument the line count (starts counting at 0). Stops the iteration
  // if the callback returns false. Returns true iff the callback always
  // returned true.
  bool EveryLine(
      const std::function<bool(language::text::LineNumber,
                               const language::text::Line&)>& callback) const;

  // Convenience wrappers of the above.
  void ForEach(
      const std::function<void(const language::text::Line&)>& callback) const;
  void ForEach(const std::function<void(std::wstring)>& callback) const;

  std::wstring ToString() const;

  template <class C>
  language::text::LineNumber upper_bound(
      const language::NonNull<std::shared_ptr<const language::text::Line>>& key,
      C compare) const {
    return language::text::LineNumber(Lines::UpperBound(lines_, key, compare));
  }

  size_t CountCharacters() const;

  enum class CursorsBehavior { kAdjust, kUnmodified };

  void insert_line(
      language::text::LineNumber line_position,
      language::NonNull<std::shared_ptr<const language::text::Line>> line,
      CursorsBehavior cursors_behavior = CursorsBehavior::kAdjust);

  // Does not call observer_! That should be done by the caller. Avoid
  // calling this in general: prefer calling the other functions (that have more
  // semantic information about what you're doing).
  void set_line(
      language::text::LineNumber position,
      language::NonNull<std::shared_ptr<const language::text::Line>> line);

  template <class C>
  void sort(language::text::LineNumber first, language::text::LineNumber last,
            C compare) {
    // TODO: Only append to `lines` the actual range [first, last), and then
    // just Append to prefix/suffix.
    std::vector<language::NonNull<std::shared_ptr<const language::text::Line>>>
        lines;
    Lines::Every(
        lines_,
        [&lines](language::NonNull<std::shared_ptr<const language::text::Line>>
                     line) {
          lines.push_back(line);
          return true;
        });
    std::sort(lines.begin() + first.read(), lines.begin() + last.read(),
              compare);
    lines_ = nullptr;
    for (auto& line : lines) {
      lines_ = Lines::PushBack(std::move(lines_), std::move(line));
    }
    observer_->Notify(infrastructure::screen::CursorsTracker::Transformation());
  }

  // If modifiers is present, applies it to every character (overriding
  // modifiers from the source).
  void insert(
      language::text::LineNumber position_line, const BufferContents& source,
      const std::optional<infrastructure::screen::LineModifierSet>& modifiers);

  // Delete characters from position.line in range [position.column,
  // position.column + amount). Amount must not be negative and it must be in a
  // valid range.
  void DeleteCharactersFromLine(
      language::text::LineColumn position,
      language::lazy_string::ColumnNumberDelta amount,
      CursorsBehavior cursors_behavior = CursorsBehavior::kAdjust);
  // Delete characters from position.line in range [position.column, ...).
  void DeleteToLineEnd(
      language::text::LineColumn position,
      CursorsBehavior cursors_behavior = CursorsBehavior::kAdjust);

  // Sets the character and modifiers in a given position.
  //
  // `position.line` must be smaller than size().
  //
  // `position.column` may be greater than size() of the current line, in which
  // case the character will just get appended (extending the line by exactly
  // one character).
  void SetCharacter(language::text::LineColumn position, int c,
                    infrastructure::screen::LineModifierSet modifiers);

  void InsertCharacter(language::text::LineColumn position);
  void AppendToLine(language::text::LineNumber line,
                    language::text::Line line_to_append);

  void EraseLines(language::text::LineNumber first,
                  language::text::LineNumber last,
                  CursorsBehavior cursors_behavior);

  void SplitLine(language::text::LineColumn position);

  // Appends the next line to the current line and removes the next line.
  // Essentially, removes the \n at the end of the current line.
  //
  // If the line is out of range, doesn't do anything.
  void FoldNextLine(language::text::LineNumber line);

  void push_back(std::wstring str);
  void push_back(
      language::NonNull<std::shared_ptr<const language::text::Line>> line);
  void append_back(
      std::vector<
          language::NonNull<std::shared_ptr<const language::text::Line>>>
          lines);

  // Returns position, but ensuring that it is in a valid position in the
  // contents — that the line is valid, and that the column fits the length of
  // the line.
  language::text::LineColumn AdjustLineColumn(
      language::text::LineColumn position) const;

  std::vector<tests::fuzz::Handler> FuzzHandlers() override;

 private:
  template <typename Callback>
  void TransformLine(
      language::text::LineNumber line_number, Callback callback,
      CursorsBehavior cursors_behavior = CursorsBehavior::kAdjust) {
    TransformLine(
        line_number, std::move(callback),
        cursors_behavior == CursorsBehavior::kAdjust
            ? std::make_optional(
                  infrastructure::screen::CursorsTracker::Transformation())
            : std::nullopt);
  }

  template <typename Callback>
  void TransformLine(
      language::text::LineNumber line_number, Callback callback,
      std::optional<infrastructure::screen::CursorsTracker::Transformation>
          cursors_transformation) {
    static infrastructure::Tracker tracker(L"BufferContents::TransformLine");
    auto tracker_call = tracker.Call();
    if (lines_ == nullptr) {
      lines_ = Lines::PushBack(nullptr, {});
    }
    CHECK_LE(line_number, EndLine());
    language::text::LineBuilder options(at(line_number).value());
    callback(options);
    set_line(line_number,
             language::MakeNonNullShared<const language::text::Line>(
                 std::move(options).Build()));
    if (cursors_transformation.has_value())
      observer_->Notify(*cursors_transformation);
  }

  Lines::Ptr lines_ = Lines::PushBack(nullptr, {});

  // TODO(2023-09-09, easy): Add const qualifier? This should be immutable. But
  // that is challenging because it disables move construction... which would be
  // fine (given that there's a copy method), but we use ListenableValues of
  // this, and ListenableValue requires moves.
  language::NonNull<std::shared_ptr<BufferContentsObserver>> observer_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_CONTENTS_H__
