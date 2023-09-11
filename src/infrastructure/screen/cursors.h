#ifndef __AFC_INFRASTRUCTURE_SCREEN_CURSORS_H__
#define __AFC_INFRASTRUCTURE_SCREEN_CURSORS_H__

#include <glog/logging.h>

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/text/line_column.h"
#include "src/language/text/mutable_line_sequence.h"

namespace afc::infrastructure::screen {
class CursorsTrackerMutableLineSequenceObserver;

// A multiset of language::text::LineColumn entries, with a specific one
// designated as the "active" one. The language::text::LineColumn entries aren't
// bound to any specific buffer, so they may exceed past the length of any and
// all buffers. The set may be empty.
class CursorsSet {
 public:
  CursorsSet() = default;
  CursorsSet(const CursorsSet& other)
      : cursors_(other.cursors_), active_(cursors_.find(*other.active_)) {}
  CursorsSet& operator=(CursorsSet other) {
    cursors_ = other.cursors_;
    active_ = cursors_.find(*other.active_);
    return *this;
  }

  using iterator = std::multiset<language::text::LineColumn>::iterator;
  using const_iterator =
      std::multiset<language::text::LineColumn>::const_iterator;

  // position must already be a value in the set (or we'll crash).
  void SetCurrentCursor(language::text::LineColumn position);

  // Remove the current cursor from the set, add a new cursor at the position,
  // and set that as the current cursor.
  void MoveCurrentCursor(language::text::LineColumn position);

  // cursors must have at least two elements.
  void DeleteCurrentCursor();

  size_t size() const;
  bool empty() const;
  iterator insert(language::text::LineColumn line);

  iterator lower_bound(language::text::LineColumn line);
  const_iterator lower_bound(language::text::LineColumn line) const;

  iterator find(language::text::LineColumn line);
  const_iterator find(language::text::LineColumn line) const;

  // Are there any cursors in a given line?
  bool cursors_in_line(language::text::LineNumber line) const;

  void erase(iterator it);
  void erase(language::text::LineColumn position);

  void swap(CursorsSet* other);

  void clear();

  template <typename iterator>
  void insert(iterator begin, iterator end) {
    cursors_.insert(begin, end);
    if (active_ == cursors_.end()) {
      active_ = this->begin();
    }
  }

  const_iterator begin() const { return cursors_.begin(); }
  const_iterator end() const { return cursors_.end(); }
  iterator begin() { return cursors_.begin(); }
  iterator end() { return cursors_.end(); }

  const_iterator active() const;
  iterator active();
  void set_active(iterator iterator);

  size_t current_index() const;

 private:
  std::multiset<language::text::LineColumn> cursors_;
  // Must be equal to cursors_.end() iff cursors_ is empty.
  std::multiset<language::text::LineColumn>::iterator active_ = cursors_.end();
};

class CursorsTracker {
 public:
  CursorsTracker();

  language::NonNull<
      std::unique_ptr<afc::language::text::MutableLineSequenceObserver>>
  NewMutableLineSequenceObserver();

  // Returns the position of the current cursor.
  language::text::LineColumn position() const;

  CursorsSet& FindOrCreateCursors(const std::wstring& name) {
    return cursors_[name];
  }

  const CursorsSet* FindCursors(const std::wstring& name) const {
    auto result = cursors_.find(name);
    return result == cursors_.end() ? nullptr : &result->second;
  }

  // Iterate over all cursors, running callback for each of them. callback
  // receives the cursor's position and must notify the receiver with the
  // position to which the cursor moves.
  futures::Value<language::EmptyValue> ApplyTransformationToCursors(
      CursorsSet& cursors,
      std::function<futures::Value<language::text::LineColumn>(
          language::text::LineColumn)>
          callback);

  // Push current cursors into cursors_stack_ and returns size of stack.
  size_t Push();
  // If cursors_stack_ isn't empty, pops from it into active cursors. Returns
  // the size the stack had at the time the call was made.
  size_t Pop();

  std::shared_ptr<bool> DelayTransformations();

 private:
  friend CursorsTrackerMutableLineSequenceObserver;

  struct Transformation;

  friend std::ostream& operator<<(std::ostream& os,
                                  const CursorsTracker::Transformation& lc);

  // Applies the transformation to every single cursor.
  void AdjustCursors(Transformation transformation);

  // Contains a transformation along with additional information that can be
  // used to optimize transformations.
  struct ExtendedTransformation;

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

  // A stack of sets of cursors on which Push and Pop operate.
  std::list<CursorsSet> cursors_stack_;

  std::weak_ptr<std::list<ExtendedTransformation>> scheduled_transformations_;
};

std::ostream& operator<<(std::ostream& os,
                         const CursorsTracker::Transformation& lc);

}  // namespace afc::infrastructure::screen
#endif  // __AFC_INFRASTRUCTURE_SCREEN_CURSORS_H__
