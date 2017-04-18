#include "cursors.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <set>
#include <unordered_set>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "wstring.h"

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
  position.line = TransformValue(
      position.line, transformation.add_to_line,
      transformation.output_line_ge,
      is_end);
  position.column = TransformValue(
      position.column, transformation.add_to_column,
      transformation.output_column_ge,
      is_end);
  return position;
}

Range CursorsTracker::Transformation::TransformRange(const Range& range) const {
  return Range(TransformLineColumn(*this, range.begin, false),
               TransformLineColumn(*this, range.end, true));
}

LineColumn CursorsTracker::Transformation::Transform(const LineColumn& position)
    const {
  return TransformLineColumn(*this, position, false);
}

Range OutputOf(const CursorsTracker::Transformation& transformation) {
  return transformation.TransformRange(transformation.range);
}

// Contains a transformation along with additional information that can be used
// to optimize transformations.
struct ExtendedTransformation {
  ExtendedTransformation(const CursorsTracker::Transformation& transformation,
                         ExtendedTransformation* previous)
      : transformation(transformation) {
    if (transformation.add_to_line > 0) {
      empty.begin = transformation.range.begin;
      empty.end = min(
          transformation.range.end,
          LineColumn(transformation.range.begin.line
                         + transformation.add_to_line,
                     transformation.range.begin.column
                         + transformation.add_to_column));
    }
    if (previous != nullptr) {
      owned = previous->empty.Intersection(OutputOf(transformation));
    }
  }

  CursorsTracker::Transformation transformation;

  // A range that is known to not have any cursors after this transformation is
  // applied.
  Range empty;

  // A range where we know that any cursors here were moved by this
  // transformation.
  Range owned;
};

std::ostream& operator<<(std::ostream& os,
                         const ExtendedTransformation& t) {
  os << "[Transformation: " << t.transformation << ", empty: " << t.empty
     << ", owned: " << t.owned << "]";
  return os;
}

CursorsTracker::CursorsTracker() {
  current_cursor_ = cursors_[L""].insert(LineColumn());
}

CursorsTracker::~CursorsTracker() {}

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
  return t.add_to_line == 0 && t.add_to_column == 0 && t.output_line_ge == 0 &&
      t.output_column_ge == 0;
}

void CursorsTracker::AdjustCursors(Transformation transformation) {
  auto output = DelayTransformations();

  // Remove unnecessary output_line_ge.
  if (transformation.add_to_line == -1 && transformation.add_to_column == 0
      && transformation.output_line_ge == transformation.range.begin.line) {
    transformation.output_line_ge = 0;
    transformation.range.begin.line++;
  }

  if (IsNoop(transformation)) {
    return;
  }

  if (transformations_.empty()) {
    transformations_.emplace_back(transformation, nullptr);
    return;
  }

  auto& last = transformations_.back();
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
      last.transformation.range.end.column
          == std::numeric_limits<size_t>::max() &&
      last.transformation.add_to_line + transformation.add_to_line == 0 &&
      last.transformation.output_line_ge == 0 &&
      last.transformation.output_column_ge == 0 &&
      last.transformation.add_to_column == 0 &&
      transformation.add_to_column == 0) {
    last.transformation.range.end.line =
        min(last.transformation.range.end.line, transformation.output_line_ge);
    last.transformation.add_to_line = min(last.transformation.add_to_line,
        static_cast<int>(transformation.output_line_ge
                             - last.transformation.range.begin.line));
    transformation = last.transformation;
    transformations_.pop_back();
    AdjustCursors(transformation);
    return;
  }

  if (last.owned == transformation.range &&
      last.transformation.range.Contains(OutputOf(transformation)) &&
      last.transformation.add_to_line + transformation.add_to_line == 0 &&
      last.transformation.add_to_line > 0 &&
      last.transformation.add_to_column < 0 &&
      transformation.add_to_column >= -last.transformation.add_to_column &&
      last.transformation.output_line_ge == 0 &&
      last.transformation.output_column_ge == 0 &&
      transformation.output_line_ge == 0 &&
      transformation.output_column_ge == 0) {
    last.transformation.add_to_line = 0;
    last.transformation.add_to_column += transformation.add_to_column;
    transformation = last.transformation;
    transformations_.pop_back();
    AdjustCursors(transformation);
    return;
  }

  // Turn:
  // [range: [[0:0], [inf:inf]), line: 1, line_ge: 0, column: 0, column_ge: 0.
  // [range: [[1:0], [5:0]), line: -1, line_ge: 0, column: A, column_ge: 0.
  // Into:
  // [range: [[4:0], [inf:inf]), line: 1, line_ge: 0, column: 0, column_ge: 0.
  // [range: [[0:0], [4:0]), line: 0, line_ge: 0, column: A, column_ge: 0.
  if (last.transformation.range.begin.line + last.transformation.add_to_line ==
          transformation.range.begin.line &&
      last.transformation.range.begin.column == 0 &&
      transformation.range.end < LineColumn::Max() &&
      transformation.range.begin.column == 0 &&
      last.transformation.range.end == LineColumn::Max() &&
      last.transformation.add_to_line > 0 &&
      transformation.add_to_line == -last.transformation.add_to_line) {
    Transformation previous = last.transformation;
    previous.range.begin.line =
        transformation.range.end.line + transformation.add_to_line;
    transformation.range.begin = last.transformation.range.begin;
    transformation.range.end.line += transformation.add_to_line;
    transformation.add_to_line = 0;
    transformations_.pop_back();
    AdjustCursors(transformation);
    AdjustCursors(previous);
    return;
  }

  if (last.transformation.add_to_column == 0 &&
      last.transformation.output_column_ge == 0 &&
      last.transformation.range.begin.column == 0 &&
      transformation.add_to_column == 0 &&
      transformation.output_column_ge == 0 &&
      transformation.range.begin.column == 0) {
    if (last.transformation.add_to_line > 0 &&
        last.transformation.range.begin.line + last.transformation.add_to_line ==
            transformation.range.begin.line &&
        transformation.add_to_line < 0 &&
        last.transformation.add_to_line >= -transformation.add_to_line &&
        last.transformation.range.end == LineColumn::Max() &&
        transformation.range.end == LineColumn::Max()) {
      last.transformation.add_to_line += transformation.add_to_line;
      transformation = last.transformation;
      transformations_.pop_back();
      AdjustCursors(transformation);
      return;
    }
    if (transformation.range.end == last.transformation.range.begin
        && transformation.add_to_line == last.transformation.add_to_line
        && transformation.add_to_line > 0) {
      last.transformation.range.begin = transformation.range.begin;
      transformation = last.transformation;
      transformations_.pop_back();
      AdjustCursors(transformation);
      return;
    } else {
      LOG(INFO) << "Skip: " << last << " - " << transformation;
    }
  }

  if (transformation.range.end == last.transformation.range.begin &&
      transformation.range.end.column == 0 &&
      transformation.add_to_line == 0 &&
      last.transformation.add_to_line >= 0) {
    // Swap the order.
    Transformation previous = last.transformation;
    transformations_.pop_back();
    AdjustCursors(transformation);
    AdjustCursors(previous);
    return;
  }

  transformations_.emplace_back(transformation, &last);
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

std::shared_ptr<bool> CursorsTracker::DelayTransformations() {
  auto output = delay_transformations_.lock();
  if (output == nullptr) {
    output = std::shared_ptr<bool>(
        new bool(),
        [this](bool *value) {
          delete value;
          LOG(INFO) << "Transformations: " << transformations_.size();
          for (auto& t : transformations_) {
            LOG(INFO) << "Applying transformation: " << t;
            ApplyTransformation(t.transformation);
          }
          transformations_.clear();
        });
    delay_transformations_ = output;
  }
  return output;
}

void CursorsTracker::ApplyTransformation(const Transformation& transformation) {
  if (transformation.add_to_line == 0 && transformation.add_to_column == 0) {
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
  os << "[range: " << t.range << ", line: " << t.add_to_line << ", line_ge: "
     << t.output_line_ge << ", column: " << t.add_to_column  << ", column_ge: "
     << t.output_column_ge << ", output: " << OutputOf(t) << "]";
  return os;
}

}  // namespace editor
}  // namespace afc
