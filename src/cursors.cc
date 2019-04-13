#include "src/cursors.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <unordered_set>

#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

bool RangeContains(const Range& range,
                   const CursorsTracker::Transformation& transformation) {
  return range.Contains(transformation.range);
}

size_t TransformValue(size_t input, int delta, size_t clamp, bool is_end) {
  if (delta < 0 && input <= clamp - delta) {
    return clamp;
  }
  if (is_end ? input == 0 : input == std::numeric_limits<size_t>::max()) {
    return input;
  }
  return input + delta;
}

LineColumn TransformLineColumn(
    const CursorsTracker::Transformation& transformation, LineColumn position,
    bool is_end) {
  position.line = TransformValue(position.line, transformation.line_delta,
                                 transformation.line_lower_bound, is_end);
  position.column = TransformValue(position.column, transformation.column_delta,
                                   transformation.column_lower_bound, is_end);
  return position;
}

Range CursorsTracker::Transformation::TransformRange(const Range& range) const {
  return Range(TransformLineColumn(*this, range.begin, false),
               TransformLineColumn(*this, range.end, true));
}

LineColumn CursorsTracker::Transformation::Transform(
    const LineColumn& position) const {
  return TransformLineColumn(*this, position, false);
}

Range OutputOf(const CursorsTracker::Transformation& transformation) {
  return transformation.TransformRange(transformation.range);
}

CursorsTracker::ExtendedTransformation::ExtendedTransformation(
    CursorsTracker::Transformation transformation,
    ExtendedTransformation* previous)
    : transformation(std::move(transformation)) {
  if (transformation.line_delta > 0) {
    empty.begin = transformation.range.begin;
    empty.end = min(
        transformation.range.end,
        LineColumn(
            transformation.range.begin.line + transformation.line_delta,
            transformation.range.begin.column + transformation.column_delta));
  }
  if (previous != nullptr) {
    owned = previous->empty.Intersection(OutputOf(transformation));
  }
}

std::wstring CursorsTracker::ExtendedTransformation::ToString() {
  // TODO: Implement.
  return L"[transformation]";
  // return L"[Transformation: " + transformation + L", empty: " + empty
  //        << ", owned: " + owned + "]";
}

CursorsTracker::CursorsTracker() {
  current_cursor_ = cursors_[L""].insert(LineColumn());
}

LineColumn CursorsTracker::position() const { return *current_cursor_; }

void CursorsTracker::SetCurrentCursor(CursorsSet* cursors,
                                      LineColumn position) {
  current_cursor_ = cursors->find(position);
  CHECK(current_cursor_ != cursors->end());
  LOG(INFO) << "Current cursor set to: " << *current_cursor_;
}

void CursorsTracker::MoveCurrentCursor(CursorsSet* cursors,
                                       LineColumn position) {
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

  // Transfer all affected cursors from cursors into cursors_affected.
  CursorsSet cursors_affected;
  auto it = cursors_set->lower_bound(transformation.range.begin);
  auto end = cursors_set->lower_bound(transformation.range.end);
  while (it != end) {
    auto result = cursors_affected.insert(*it);
    if (it == *current_cursor) {
      *current_cursor = result;
    }
    cursors_set->erase(it++);
  }

  // Apply the transformation and add the cursors back.
  for (auto it = cursors_affected.begin(); it != cursors_affected.end(); ++it) {
    auto position = transformation.Transform(*it);

    auto result = cursors_set->insert(position);
    if (it == *current_cursor) {
      *current_cursor = result;
    }
  }
}

bool IsNoop(const CursorsTracker::Transformation& t) {
  return t.line_delta == 0 && t.column_delta == 0 && t.line_lower_bound == 0 &&
         t.column_lower_bound == 0;
}

void CursorsTracker::AdjustCursors(Transformation transformation) {
  auto transformations = scheduled_transformations();

  // Remove unnecessary line_lower_bound.
  if (transformation.line_delta == -1 && transformation.column_delta == 0 &&
      transformation.line_lower_bound == transformation.range.begin.line) {
    transformation.line_lower_bound = 0;
    transformation.range.begin.line++;
  }

  if (IsNoop(transformation)) {
    return;
  }

  if (transformations->empty()) {
    transformations->emplace_back(transformation, nullptr);
    return;
  }

  auto& last = transformations->back();
  if (last.empty.Contains(transformation.range)) {
    // All cursors in transformation have been moved by last.
    LOG(INFO) << "Removed: " << transformation;
    return;
  }

  // Collapse:
  // [[A:0], [B:MAX]), line: C, line_ge: 0, column: 0, column_ge: 0
  // [[A:0], [B:MAX]), line: -C, line_ge: D, column: 0, column_ge: 0
  //
  // Into:
  // [[A:0], [min(B, D):MAX]), line: C, line_ge: 0, column: 0, column_ge: 0
  if (last.transformation.range == transformation.range &&
      last.transformation.range.begin.column == 0 &&
      last.transformation.range.end.column ==
          std::numeric_limits<size_t>::max() &&
      last.transformation.line_delta + transformation.line_delta == 0 &&
      last.transformation.line_lower_bound == 0 &&
      last.transformation.column_lower_bound == 0 &&
      last.transformation.column_delta == 0 &&
      transformation.column_delta == 0) {
    last.transformation.range.end.line = min(last.transformation.range.end.line,
                                             transformation.line_lower_bound);
    last.transformation.line_delta =
        min(last.transformation.line_delta,
            static_cast<int>(transformation.line_lower_bound -
                             last.transformation.range.begin.line));
    transformation = last.transformation;
    transformations->pop_back();
    AdjustCursors(transformation);
    return;
  }

  if (last.owned == transformation.range &&
      last.transformation.range.Contains(OutputOf(transformation)) &&
      last.transformation.line_delta + transformation.line_delta == 0 &&
      last.transformation.line_delta > 0 &&
      last.transformation.column_delta < 0 &&
      transformation.column_delta >= -last.transformation.column_delta &&
      last.transformation.line_lower_bound == 0 &&
      last.transformation.column_lower_bound == 0 &&
      transformation.line_lower_bound == 0 &&
      transformation.column_lower_bound == 0) {
    last.transformation.line_delta = 0;
    last.transformation.column_delta += transformation.column_delta;
    transformation = last.transformation;
    transformations->pop_back();
    AdjustCursors(transformation);
    return;
  }

  // Turn:
  // [range: [[0:0], [inf:inf]), line: 1, line_ge: 0, column: 0, column_ge: 0.
  // [range: [[1:0], [5:0]), line: -1, line_ge: 0, column: A, column_ge: 0.
  //
  // Into:
  // [range: [[4:0], [inf:inf]), line: 1, line_ge: 0, column: 0, column_ge: 0.
  // [range: [[0:0], [4:0]), line: 0, line_ge: 0, column: A, column_ge: 0.
  if (last.transformation.range.begin.line + last.transformation.line_delta ==
          transformation.range.begin.line &&
      last.transformation.range.begin.column == 0 &&
      transformation.range.end < LineColumn::Max() &&
      transformation.range.begin.column == 0 &&
      last.transformation.range.end == LineColumn::Max() &&
      last.transformation.line_delta > 0 &&
      transformation.line_delta == -last.transformation.line_delta) {
    Transformation previous = last.transformation;
    previous.range.begin.line =
        transformation.range.end.line + transformation.line_delta;
    transformation.range.begin = last.transformation.range.begin;
    transformation.range.end.line += transformation.line_delta;
    transformation.line_delta = 0;
    transformations->pop_back();
    AdjustCursors(transformation);
    AdjustCursors(previous);
    return;
  }

  if (last.transformation.column_delta == 0 &&
      last.transformation.column_lower_bound == 0 &&
      last.transformation.range.begin.column == 0 &&
      transformation.column_delta == 0 &&
      transformation.column_lower_bound == 0 &&
      transformation.range.begin.column == 0) {
    if (last.transformation.line_delta > 0 &&
        last.transformation.range.begin.line + last.transformation.line_delta ==
            transformation.range.begin.line &&
        transformation.line_delta < 0 &&
        last.transformation.line_delta >= -transformation.line_delta &&
        last.transformation.range.end == LineColumn::Max() &&
        transformation.range.end == LineColumn::Max()) {
      last.transformation.line_delta += transformation.line_delta;
      transformation = last.transformation;
      transformations->pop_back();
      AdjustCursors(transformation);
      return;
    }
    if (transformation.range.end == last.transformation.range.begin &&
        transformation.line_delta == last.transformation.line_delta &&
        transformation.line_delta > 0) {
      last.transformation.range.begin = transformation.range.begin;
      transformation = last.transformation;
      transformations->pop_back();
      AdjustCursors(transformation);
      return;
    } else {
      LOG(INFO) << "Skip: " << last.ToString() << " - " << transformation;
    }
  }

  if (transformation.range.end == last.transformation.range.begin &&
      transformation.range.end.column == 0 && transformation.line_delta == 0 &&
      last.transformation.line_delta >= 0) {
    // Swap the order.
    Transformation previous = last.transformation;
    transformations->pop_back();
    AdjustCursors(transformation);
    AdjustCursors(previous);
    return;
  }

  transformations->emplace_back(transformation, &last);
}

void CursorsTracker::ApplyTransformationToCursors(
    CursorsSet* cursors, std::function<LineColumn(LineColumn)> callback) {
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

std::shared_ptr<bool> CursorsTracker::DelayTransformations() {
  auto shared_lock = scheduled_transformations();
  return std::shared_ptr<bool>(new bool(),
                               [shared_lock](bool* value) { delete value; });
}

std::shared_ptr<std::list<CursorsTracker::ExtendedTransformation>>
CursorsTracker::scheduled_transformations() {
  auto output = scheduled_transformations_.lock();
  if (output != nullptr) {
    return output;
  }
  output = std::shared_ptr<std::list<ExtendedTransformation>>(
      new std::list<ExtendedTransformation>(),
      [this](std::list<ExtendedTransformation>* transformations) {
        for (auto& t : *transformations) {
          ApplyTransformation(t.transformation);
        }
        delete transformations;
      });
  scheduled_transformations_ = output;
  return output;
}

void CursorsTracker::ApplyTransformation(const Transformation& transformation) {
  if (transformation.line_delta == 0 && transformation.column_delta == 0) {
    return;
  }
  for (auto& cursors_set : cursors_) {
    AdjustCursorsSet(transformation, &cursors_set.second, &current_cursor_);
  }
  for (auto& cursors_set : cursors_stack_) {
    AdjustCursorsSet(transformation, &cursors_set, &current_cursor_);
  }
  AdjustCursorsSet(transformation, &already_applied_cursors_, &current_cursor_);
}

std::ostream& operator<<(std::ostream& os,
                         const CursorsTracker::Transformation& t) {
  os << "[range: " << t.range << ", line: " << t.line_delta
     << ", line_ge: " << t.line_lower_bound << ", column: " << t.column_delta
     << ", column_ge: " << t.column_lower_bound << ", output: " << OutputOf(t)
     << "]";
  return os;
}

}  // namespace editor
}  // namespace afc
