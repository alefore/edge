#ifndef __AFC_EDITOR_CURSORS_H__
#define __AFC_EDITOR_CURSORS_H__

#include <glog/logging.h>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "src/line_column.h"

namespace afc {
namespace editor {

// A multiset of LineColumn entries, with a specific one designated as the
// "active" one. The LineColumn entries aren't bound to any specific buffer, so
// they may exceed past the length of any and all buffers. The set may be empty.
class CursorsSet {
 public:
  using Iterator = std::multiset<LineColumn>::iterator;
  using const_iterator = std::multiset<LineColumn>::const_iterator;

  // position must already be a value in the set (or we'll crash).
  void SetCurrentCursor(LineColumn position);

  // Remove the current cursor from the set, add a new cursor at the position,
  // and set that as the current cursor.
  void MoveCurrentCursor(LineColumn position);

  // cursors must have at least two elements.
  void DeleteCurrentCursor();

  size_t size();
  bool empty();
  Iterator insert(LineColumn line);
  Iterator lower_bound(LineColumn line);
  Iterator find(LineColumn line);

  void erase(Iterator it);
  void erase(LineColumn position);

  void swap(CursorsSet* other);

  void clear();

  template <typename Iterator>
  void insert(Iterator begin, Iterator end) {
    cursors_.insert(begin, end);
    if (active_ == cursors_.end()) {
      active_ = this->begin();
    }
  }

  const_iterator begin() const { return cursors_.begin(); }
  const_iterator end() const { return cursors_.end(); }
  Iterator begin() { return cursors_.begin(); }
  Iterator end() { return cursors_.end(); }

  const_iterator active() const;
  Iterator active();
  void set_active(Iterator iterator);

 private:
  std::multiset<LineColumn> cursors_;
  // Must be equal to cursors_.end() iff cursors_ is empty.
  std::multiset<LineColumn>::iterator active_ = cursors_.end();
};

class CursorsTracker {
 public:
  struct Transformation {
    Transformation& WithBegin(LineColumn position) {
      CHECK_EQ(range.begin, LineColumn());
      range.begin = position;
      return *this;
    }

    Transformation& WithEnd(LineColumn position) {
      CHECK_EQ(range.end, LineColumn::Max());
      range.end = position;
      return *this;
    }

    Transformation& WithLineEq(size_t line) {
      range.begin = LineColumn(line);
      range.end = LineColumn(line + 1);
      return *this;
    }

    CursorsTracker::Transformation LineDelta(size_t delta) {
      line_delta = delta;
      return *this;
    }

    CursorsTracker::Transformation LineLowerBound(size_t line) {
      line_lower_bound = line;
      return *this;
    }

    CursorsTracker::Transformation ColumnDelta(size_t delta) {
      column_delta = delta;
      return *this;
    }

    CursorsTracker::Transformation ColumnLowerBound(size_t column) {
      column_lower_bound = column;
      return *this;
    }

    LineColumn Transform(const LineColumn& position) const;
    Range TransformRange(const Range& range) const;

    Range range = Range(LineColumn(), LineColumn::Max());

    // Number of lines to add to a given cursor. For example, a cursor
    // LineColumn(25, 2) will move to LineColumn(20, 2) if lines_delta is -5.
    int line_delta = 0;

    // If lines_delta would leave the output line at a value smaller than this
    // one, goes with this one.
    size_t line_lower_bound = 0;

    int column_delta = 0;
    // Same as output_line_ge but for column computations.
    size_t column_lower_bound = 0;
  };

  CursorsTracker();

  // Returns the position of the current cursor.
  LineColumn position() const;

  CursorsSet* FindOrCreateCursors(const std::wstring& name) {
    return &cursors_[name];
  }

  const CursorsSet* FindCursors(const std::wstring& name) const {
    auto result = cursors_.find(name);
    return result == cursors_.end() ? nullptr : &result->second;
  }

  // Applies the callback to every single cursor and leaves it at the returned
  // position.
  void AdjustCursors(Transformation transformation);

  void ApplyTransformationToCursors(
      CursorsSet* cursors, std::function<LineColumn(LineColumn)> callback);

  // Push current cursors into cursors_stack_ and returns size of stack.
  size_t Push();
  // If cursors_stack_ isn't empty, pops from it into active cursors. Returns
  // the size the stack had at the time the call was made.
  size_t Pop();

  std::shared_ptr<bool> DelayTransformations();

 private:
  // Contains a transformation along with additional information that can be
  // used to optimize transformations.
  struct ExtendedTransformation {
    ExtendedTransformation(CursorsTracker::Transformation transformation,
                           ExtendedTransformation* previous);

    std::wstring ToString();

    CursorsTracker::Transformation transformation;

    // A range that is known to not have any cursors after this transformation
    // is applied.
    Range empty;

    // A range where we know that any cursors here were moved by this
    // transformation.
    Range owned;
  };

  std::shared_ptr<std::list<ExtendedTransformation>>
  scheduled_transformations();

  void ApplyTransformation(const Transformation& transformation);

  // Contains a family of cursors.
  std::map<std::wstring, CursorsSet> cursors_;

  // While we're applying a transformation to a set of cursors, we need to
  // remember what cursors it has already been applied to. To do that, we
  // gradually drain the original set of cursors and add them here as we apply
  // the transformation to them. We can't just loop over the set of cursors
  // since each transformation will likely reshuffle them. Once the source of
  // cursors to modify is empty, we just swap it back with this.
  CursorsSet already_applied_cursors_;

  // A key in cursors_.
  std::wstring active_set_;

  // A stack of sets of cursors on which PushActiveCursors and PopActiveCursors
  // operate.
  std::list<CursorsSet> cursors_stack_;

  std::weak_ptr<std::list<ExtendedTransformation>> scheduled_transformations_;
};

std::ostream& operator<<(std::ostream& os,
                         const CursorsTracker::Transformation& lc);

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CURSORS_H__
