#include "cursors.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "wstring.h"

namespace afc {
namespace editor {

CursorsTracker::CursorsTracker() {
  current_cursor_ = cursors_[L""].insert(LineColumn());
}

LineColumn CursorsTracker::position() const {
  return *current_cursor_;
}

void CursorsTracker::SetCurrentCursor(
    CursorsSet* cursors, LineColumn position) {
  current_cursor_ = cursors->find(position);
  CHECK(current_cursor_ != cursors->end());
  LOG(INFO) << "Current cursor set to: " << *current_cursor_;
}

void CursorsTracker::MoveCurrentCursor(
    CursorsSet* cursors, LineColumn position) {
  cursors->insert(position);
  DeleteCurrentCursor(cursors);
  SetCurrentCursor(cursors, position);
}

void CursorsTracker::DeleteCurrentCursor(CursorsSet* cursors) {
  CHECK(cursors != nullptr);
  CHECK(cursors->size() > 1) << "Attempted to delete the last cursor in set.";
  cursors->erase(current_cursor_++);
  if (current_cursor_ == cursors->end()) {
    current_cursor_ = cursors->begin();
  }
}

void AdjustCursorsSet(const CursorsTracker::Transformation& transformation,
                      CursorsSet* cursors_set,
                      CursorsSet::iterator* current_cursor) {
  VLOG(8) << "Adjusting cursor set of size: " << cursors_set->size();
  CursorsSet old_cursors;
  cursors_set->swap(old_cursors);
  for (auto it = old_cursors.begin(); it != old_cursors.end(); ++it) {
    VLOG(9) << "Adjusting cursor: " << *it;
    auto result = cursors_set->insert(transformation.callback(*it));
    if (it == *current_cursor) {
      VLOG(5) << "Updating current cursor: " << *result;
      *current_cursor = result;
    }
  }
}

void CursorsTracker::AdjustCursors(const Transformation& transformation) {
  if (!transformation.callback) { return; }
  for (auto& cursors_set : cursors_) {
    AdjustCursorsSet(transformation, &cursors_set.second, &current_cursor_);
  }
  for (auto& cursors_set : cursors_stack_) {
    AdjustCursorsSet(transformation, &cursors_set, &current_cursor_);
  }
  AdjustCursorsSet(transformation, &already_applied_cursors_, &current_cursor_);
}

void CursorsTracker::ApplyTransformationToCursors(
    CursorsSet* cursors,
    std::function<LineColumn(LineColumn)> callback) {
  CHECK(cursors != nullptr);
  LOG(INFO) << "Applying transformation to cursors: " << cursors->size();
  CHECK(already_applied_cursors_.empty());
  bool adjusted_current_cursor = false;
  while (!cursors->empty()) {
    auto new_position = callback(*cursors->begin());

    auto insert_result = already_applied_cursors_.insert(new_position);
    if (cursors->begin() == current_cursor_) {
      VLOG(6) << "Adjusting default cursor (multiple): " << *insert_result;
      current_cursor_ = insert_result;
      adjusted_current_cursor = true;
    }
    cursors->erase(cursors->begin());
  }

  cursors->swap(already_applied_cursors_);
  CHECK(adjusted_current_cursor);
  LOG(INFO) << "Current cursor at: " << *current_cursor_;
}

size_t CursorsTracker::Push() {
  cursors_stack_.push_back(*FindCursors(L""));
  return cursors_stack_.size();
}

size_t CursorsTracker::Pop() {
  if (cursors_stack_.empty()) {
    return 0;
  }

  cursors_[L""].swap(cursors_stack_.back());
  cursors_stack_.pop_back();
  current_cursor_ = cursors_[L""].begin();

  return cursors_stack_.size() + 1;
}

}  // namespace editor
}  // namespace afc
