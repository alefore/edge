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

size_t TransformValue(size_t input, int delta, size_t clamp) {
  if (delta < 0 && input <= clamp - delta) {
    return clamp;
  }
  return input + delta;
}

void AdjustCursorsSet(const CursorsTracker::Transformation& transformation,
                      CursorsSet* cursors_set,
                      CursorsSet::iterator* current_cursor) {
  VLOG(8) << "Adjusting cursor set of size: " << cursors_set->size();

  // Transfer all affected cursors from cursors into cursors_affected.
  CursorsSet cursors_affected;
  auto it = cursors_set->lower_bound(transformation.begin);
  auto end = cursors_set->upper_bound(transformation.end);
  while (it != end) {
    auto result = cursors_affected.insert(*it);
    if (it == *current_cursor) {
      *current_cursor = result;
    }
    cursors_set->erase(it++);
  }

  // Apply the transformation and add the cursors back.
  for (auto it = cursors_affected.begin(); it != cursors_affected.end(); ++it) {
    auto position = *it;
    position.line = TransformValue(
        position.line, transformation.add_to_line,
        transformation.output_line_ge);
    position.column = TransformValue(
        position.column, transformation.add_to_column,
        transformation.output_column_ge);

    auto result = cursors_set->insert(position);
    if (it == *current_cursor) {
      *current_cursor = result;
    }
  }
}

void CursorsTracker::AdjustCursors(const Transformation& transformation) {
  auto output = DelayTransformations();
  transformations_.push_back(transformation);
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
          OptimizeTransformations();
          for (auto& t : transformations_) {
            ApplyTransformation(t);
          }
          transformations_.clear();
        });
    delay_transformations_ = output;
  }
  return output;
}

bool Disjoint(const CursorsTracker::Transformation& a,
              const CursorsTracker::Transformation& b) {
  return a.end <= b.begin || a.begin >= b.end;
}

void AddToTransformation(
    const CursorsTracker::Transformation delta,
    CursorsTracker::Transformation* output) {
  output->add_to_line += delta.add_to_line;
  output->add_to_column += delta.add_to_column;
}

bool IsNoop(const CursorsTracker::Transformation& t) {
  return t.add_to_line == 0 && t.add_to_column == 0 && t.output_line_ge == 0 &&
      t.output_column_ge == 0;
}

void CursorsTracker::OptimizeTransformations() {
  LOG(INFO) << "Optimize transformations.";
  for (const auto& t : transformations_) {
    LOG(INFO) << "T: " << t;
  }

  std::list<CursorsTracker::Transformation> new_transformations;
  for (const auto& t : transformations_) {
    if (IsNoop(t)) {
      continue;
    }
    if (new_transformations.empty()) {
      new_transformations.push_back(t);
      continue;
    }

    auto& last = new_transformations.back();
    if (last.begin == t.begin &&
        t.end <= min(last.end,
                     LineColumn(last.begin.line + last.add_to_line,
                                last.begin.column + last.add_to_column))) {
      // All cursors in t have been moved by last.
      LOG(INFO) << "Removed: " << t;
      continue;
    }

    // Collapse:
    // [[A:0], [B:MAX]), line: C, line_ge: 0, column: 0, column_ge: 0
    // [[A:0], [B:MAX]), line: -C, line_ge: D, column: 0, column_ge: 0
    //
    // Into:
    // [[A:0], [min(B, D):MAX]), line: C, line_ge: 0, column: 0, column_ge: 0
    if (last.begin == t.begin &&
        last.end == t.end &&
        last.begin.column == 0 &&
        last.end.column == std::numeric_limits<size_t>::max() &&
        last.add_to_line + t.add_to_line == 0 &&
        last.output_line_ge == 0 &&
        last.output_column_ge == 0 &&
        last.add_to_column == 0 &&
        t.add_to_column == 0) {
      last.end.line = min(last.end.line, t.output_line_ge);
      last.add_to_line = min(last.add_to_line,
          static_cast<int>(t.output_line_ge - last.begin.line));
      if (IsNoop(last)) {
        LOG(INFO) << "Removing no-op transformation: " << last;
        new_transformations.pop_back();
      }
      continue;
    }

    new_transformations.push_back(t);
  }
  transformations_.swap(new_transformations);

  for (const auto& t : transformations_) {
    LOG(INFO) << "Opt: " << t;
  }

  LOG(INFO) << "Total transformations: " << transformations_.size();
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
  os << "[range: [" << t.begin << ", " << t.end << "), line: " << t.add_to_line
     << ", line_ge: " << t.output_line_ge << ", column: " << t.add_to_column
     << ", column_ge: " << t.output_column_ge << "]";
  return os;
}

}  // namespace editor
}  // namespace afc
