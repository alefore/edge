#ifndef __AFC_EDITOR_CURSORS_H__
#define __AFC_EDITOR_CURSORS_H__

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <glog/logging.h>

#include "src/line_column.h"

namespace afc {
namespace editor {

typedef std::multiset<LineColumn> CursorsSet;

class CursorsTracker {
 public:
  struct Transformation {
    Transformation& WithBegin(LineColumn position) {
      CHECK_EQ(begin, LineColumn());
      begin = position;
      return *this;
    }

    Transformation& WithEnd(LineColumn position) {
      CHECK_EQ(end, LineColumn::Max());
      end = position;
      return *this;
    }

    Transformation& WithLineEq(size_t line) {
      begin.line = line;
      end.line = line;
      return *this;
    }

    CursorsTracker::Transformation DownBy(size_t delta) {
      return WithCallback([delta](LineColumn position) {
        position.line += delta;
        return position;
      });
    }

    CursorsTracker::Transformation WithCallback(
        std::function<LineColumn(LineColumn)> callback) {
      CHECK(!this->callback);
      this->callback = std::move(callback);
      return *this;
    }

    LineColumn begin;
    LineColumn end = LineColumn::Max();

    std::function<LineColumn(LineColumn)> callback;
  };

  CursorsTracker();

  // Returns the position of the current cursor.
  LineColumn position() const;

  // cursors *must* be a value in cursors_ and position must already be a value
  // in that set (we verify the later, not the former).
  void SetCurrentCursor(CursorsSet* cursors, LineColumn position);

  // Remove the current cursor from the set, add a new cursor at the position,
  // and set that as the current cursor.
  void MoveCurrentCursor(CursorsSet* cursors, LineColumn position);

  // current_cursor_ must be a value in cursors. cursors must have at least two
  // elements.
  void DeleteCurrentCursor(CursorsSet* cursors);

  CursorsSet* FindOrCreateCursors(const std::wstring& name) {
    return &cursors_[name];
  }

  const CursorsSet* FindCursors(const std::wstring& name) const {
    auto result = cursors_.find(name);
    return result == cursors_.end() ? nullptr : &result->second;
  }

  // Applies the callback to every single cursor and leaves it at the returned
  // position.
  void AdjustCursors(const Transformation& transformation);

  void ApplyTransformationToCursors(
      CursorsSet* cursors,
      std::function<LineColumn(LineColumn)> callback);

  // Push current cursors into cursors_stack_ and returns size of stack.
  size_t Push();
  // If cursors_stack_ isn't empty, pops from it into active cursors. Returns
  // the size the stack had at the time the call was made.
  size_t Pop();

 private:
  // Contains a family of cursors.
  std::map<std::wstring, CursorsSet> cursors_;

  // While we're applying a transformation to a set of cursors, we need to
  // remember what cursors it has already been applied to. To do that, we
  // gradually drain the original set of cursors and add them here as we apply
  // the transformation to them. We can't just loop over the set of cursors
  // since each transformation will likely reshuffle them. Once the source of
  // cursors to modify is empty, we just swap it back with this.
  CursorsSet already_applied_cursors_;

  // Points to an entry in a value in cursors_.
  CursorsSet::iterator current_cursor_;

  // A stack of sets of cursors on which PushActiveCursors and PopActiveCursors
  // operate.
  std::list<CursorsSet> cursors_stack_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_CURSORS_H__
