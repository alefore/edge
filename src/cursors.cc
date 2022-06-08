#include "src/cursors.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <unordered_set>

#include "src/char_buffer.h"
#include "src/language/lazy_string/append.h"
#include "src/language/wstring.h"
#include "src/substring.h"

namespace afc::editor {
using language::EmptyValue;
using language::MakeNonNullShared;
using language::NonNull;
using ::operator<<;

void CursorsSet::SetCurrentCursor(LineColumn position) {
  active_ = cursors_.find(position);
  CHECK(active_ != cursors_.end());
  LOG(INFO) << "Current cursor set to: " << *active_;
}

void CursorsSet::MoveCurrentCursor(LineColumn position) {
  cursors_.insert(position);
  DeleteCurrentCursor();
  SetCurrentCursor(position);
}

void CursorsSet::DeleteCurrentCursor() {
  CHECK(cursors_.size() > 1) << "Attempted to delete the last cursor in set.";
  erase(active_);
}

size_t CursorsSet::size() const { return cursors_.size(); }

bool CursorsSet::empty() const { return cursors_.empty(); }

CursorsSet::iterator CursorsSet::insert(LineColumn line) {
  iterator it = cursors_.insert(line);
  if (active_ == cursors_.end()) {
    active_ = it;
  }
  return it;
}

CursorsSet::iterator CursorsSet::lower_bound(LineColumn line) {
  return cursors_.lower_bound(line);
}

CursorsSet::const_iterator CursorsSet::lower_bound(LineColumn line) const {
  return const_cast<CursorsSet*>(this)->lower_bound(line);
}

CursorsSet::iterator CursorsSet::find(LineColumn line) {
  return cursors_.find(line);
}

CursorsSet::const_iterator CursorsSet::find(LineColumn line) const {
  return const_cast<CursorsSet*>(this)->find(line);
}

bool CursorsSet::cursors_in_line(LineNumber line) const {
  auto it = lower_bound(LineColumn(line));
  return it != end() && it->line == line;
}

void CursorsSet::erase(iterator it) {
  CHECK(it != end());
  if (it == active_) {
    active_++;
    if (active_ == cursors_.end() && it != cursors_.begin()) {
      active_ = cursors_.begin();
    }
  }
  cursors_.erase(it);
}

void CursorsSet::erase(LineColumn position) {
  auto it = cursors_.find(position);
  if (it != cursors_.end()) {
    erase(it);
  }
}

void CursorsSet::swap(CursorsSet* other) {
  size_t active_distance = std::distance(begin(), active_);
  size_t other_active_distance = std::distance(other->begin(), other->active_);
  cursors_.swap(other->cursors_);
  active_ = begin();
  other->active_ = other->begin();
  std::advance(active_, other_active_distance);
  std::advance(other->active_, active_distance);
}

void CursorsSet::clear() {
  cursors_.clear();
  active_ = cursors_.end();
}

CursorsSet::const_iterator CursorsSet::active() const {
  CHECK((active_ == cursors_.end()) == cursors_.empty());
  return active_;
}

CursorsSet::iterator CursorsSet::active() {
  CHECK((active_ == cursors_.end()) == cursors_.empty());
  return active_;
}

void CursorsSet::set_active(iterator input_iterator) {
  active_ = input_iterator;
  CHECK((active_ == cursors_.end()) == cursors_.empty());
  CHECK(cursors_.find(*input_iterator) != cursors_.end());
}

size_t CursorsSet::current_index() const {
  return empty() ? 0 : std::distance(begin(), active());
}

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
  position.line = LineNumber(
      TransformValue(position.line.read(), transformation.line_delta.read(),
                     transformation.line_lower_bound.read(), is_end));
  position.column = ColumnNumber(
      TransformValue(position.column.read(), transformation.column_delta.read(),
                     transformation.column_lower_bound.read(), is_end));
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
  if (transformation.line_delta > LineNumberDelta(0)) {
    empty.begin = transformation.range.begin;
    empty.end = std::min(
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

CursorsTracker::CursorsTracker() : active_set_(L"") {
  cursors_[active_set_].insert(LineColumn());
}

LineColumn CursorsTracker::position() const {
  CHECK_EQ(cursors_.count(active_set_), 1ul);
  return *cursors_.find(active_set_)->second.active();
}

void AdjustCursorsSet(const CursorsTracker::Transformation& transformation,
                      CursorsSet* cursors_set) {
  VLOG(8) << "Adjusting cursor set of size: " << cursors_set->size();

  // Transfer affected cursors from cursors into cursors_affected.
  CursorsSet cursors_affected;
  auto it = cursors_set->lower_bound(transformation.range.begin);
  auto end = cursors_set->lower_bound(transformation.range.end);
  bool transferred_active = false;
  while (it != end) {
    auto result = cursors_affected.insert(*it);
    if (it == cursors_set->active() && !transferred_active) {
      transferred_active = true;
      cursors_affected.set_active(result);
    }
    cursors_set->erase(it++);
  }

  // Apply the transformation and add the cursors back.
  for (auto it = cursors_affected.begin(); it != cursors_affected.end(); ++it) {
    auto position = transformation.Transform(*it);

    auto result = cursors_set->insert(position);
    if (transferred_active && cursors_affected.active() == it) {
      cursors_set->set_active(result);
    }
  }
}

bool IsNoop(const CursorsTracker::Transformation& t) {
  return t.line_delta == LineNumberDelta(0) &&
         t.column_delta == ColumnNumberDelta(0) &&
         t.line_lower_bound == LineNumber(0) && t.column_lower_bound.IsZero();
}

void CursorsTracker::AdjustCursors(Transformation transformation) {
  VLOG(3) << "AdjustCursors: " << transformation;
  auto transformations = scheduled_transformations();

  // Remove unnecessary line_lower_bound.
  if (transformation.line_delta == LineNumberDelta(-1) &&
      transformation.column_delta == ColumnNumberDelta(0) &&
      transformation.line_lower_bound == transformation.range.begin.line) {
    VLOG(4) << "Remove unnecessary line_lower_bound: " << transformation;
    transformation.line_lower_bound = LineNumber(0);
    ++transformation.range.begin.line;
  }

  if (IsNoop(transformation)) {
    VLOG(4) << "Skip noop: " << transformation;
    return;
  }

  if (transformations->empty()) {
    VLOG(4) << "Empty transformation: " << transformation;
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
      last.transformation.range.begin.column.IsZero() &&
      last.transformation.range.end.column ==
          std::numeric_limits<ColumnNumber>::max() &&
      last.transformation.line_delta + transformation.line_delta ==
          LineNumberDelta(0) &&
      last.transformation.line_lower_bound == LineNumber(0) &&
      last.transformation.column_lower_bound.IsZero() &&
      last.transformation.column_delta == ColumnNumberDelta(0) &&
      transformation.column_delta == ColumnNumberDelta(0)) {
    VLOG(4) << "Collapsing transformations: " << last.transformation << " and "
            << transformation;

    last.transformation.range.end.line = std::min(
        last.transformation.range.end.line, transformation.line_lower_bound);
    last.transformation.line_delta = std::min(
        last.transformation.line_delta,
        transformation.line_lower_bound - last.transformation.range.begin.line);
    transformation = last.transformation;
    transformations->pop_back();
    AdjustCursors(transformation);
    return;
  }

  if (last.owned == transformation.range &&
      last.transformation.range.Contains(OutputOf(transformation)) &&
      last.transformation.line_delta + transformation.line_delta ==
          LineNumberDelta(0) &&
      last.transformation.line_delta > LineNumberDelta(0) &&
      last.transformation.column_delta < ColumnNumberDelta(0) &&
      transformation.column_delta >= -last.transformation.column_delta &&
      last.transformation.line_lower_bound == LineNumber(0) &&
      last.transformation.column_lower_bound.IsZero() &&
      transformation.line_lower_bound == LineNumber(0) &&
      transformation.column_lower_bound.IsZero()) {
    VLOG(4) << "Collapsing transformations: " << last.transformation << " and "
            << transformation;
    last.transformation.line_delta = LineNumberDelta(0);
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
      last.transformation.range.begin.column.IsZero() &&
      transformation.range.end < LineColumn::Max() &&
      transformation.range.begin.column.IsZero() &&
      last.transformation.range.end == LineColumn::Max() &&
      last.transformation.line_delta > LineNumberDelta(0) &&
      transformation.line_delta == -last.transformation.line_delta) {
    VLOG(4) << "Collapsing transformations: " << last.transformation << " and "
            << transformation;
    Transformation previous = last.transformation;
    previous.range.begin.line =
        transformation.range.end.line + transformation.line_delta;
    transformation.range.begin = last.transformation.range.begin;
    transformation.range.end.line += transformation.line_delta;
    transformation.line_delta = LineNumberDelta(0);
    transformations->pop_back();
    AdjustCursors(transformation);
    AdjustCursors(previous);
    return;
  }

  if (last.transformation.column_delta == ColumnNumberDelta(0) &&
      last.transformation.column_lower_bound.IsZero() &&
      last.transformation.range.begin.column.IsZero() &&
      transformation.column_delta == ColumnNumberDelta(0) &&
      transformation.column_lower_bound.IsZero() &&
      transformation.range.begin.column.IsZero()) {
    if (last.transformation.line_delta > LineNumberDelta(0) &&
        last.transformation.range.begin.line + last.transformation.line_delta ==
            transformation.range.begin.line &&
        transformation.line_delta < LineNumberDelta(0) &&
        last.transformation.line_delta >= -transformation.line_delta &&
        last.transformation.range.end == LineColumn::Max() &&
        transformation.range.end == LineColumn::Max()) {
      VLOG(4) << "Collapsing transformations: " << last.transformation
              << " and " << transformation;
      last.transformation.line_delta += transformation.line_delta;
      transformation = last.transformation;
      transformations->pop_back();
      AdjustCursors(transformation);
      return;
    }
    if (transformation.range.end == last.transformation.range.begin &&
        transformation.line_delta == last.transformation.line_delta &&
        transformation.line_delta > LineNumberDelta(0)) {
      VLOG(4) << "Collapsing transformations: " << last.transformation
              << " and " << transformation;
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
      transformation.range.end.column.IsZero() &&
      transformation.line_delta == LineNumberDelta(0) &&
      last.transformation.line_delta >= LineNumberDelta(0)) {
    VLOG(4) << "Collapsing transformations: " << last.transformation << " and "
            << transformation;
    // Swap the order.
    Transformation previous = last.transformation;
    transformations->pop_back();
    AdjustCursors(transformation);
    AdjustCursors(previous);
    return;
  }

  VLOG(4) << "Inserting transformation: " << transformation;

  transformations->emplace_back(transformation, &last);
}

futures::Value<EmptyValue> CursorsTracker::ApplyTransformationToCursors(
    CursorsSet& cursors,
    std::function<futures::Value<LineColumn>(LineColumn)> callback) {
  struct Data {
    CursorsSet& cursors;
    std::function<futures::Value<LineColumn>(LineColumn)> callback;
    futures::Value<EmptyValue>::Consumer done;
    bool adjusted_active_cursor = false;
  };

  futures::Future<EmptyValue> output;
  NonNull<std::shared_ptr<Data>> data =
      MakeNonNullShared<Data>(Data{.cursors = cursors,
                                   .callback = std::move(callback),
                                   .done = std::move(output.consumer)});
  // TODO(easy, 2022-04-30): Check that cursors.active() is valid?
  LOG(INFO) << "Applying transformation to cursors: " << cursors.size()
            << ", active is: " << *cursors.active();
  auto apply_next = [this, data](auto apply_next) {
    CHECK(data->callback != nullptr);
    if (data->cursors.empty()) {
      data->cursors.swap(&already_applied_cursors_);
      LOG(INFO) << "Current cursor at: " << *data->cursors.active();
      data->done(EmptyValue());
      return;
    }
    VLOG(6) << "Adjusting cursor: " << *data->cursors.begin();
    data->callback(*data->cursors.begin())
        .SetConsumer([this, data, apply_next](LineColumn column) {
          auto insert_result = already_applied_cursors_.insert(column);
          VLOG(7) << "Cursor moved to: " << *insert_result;
          if (!data->adjusted_active_cursor &&
              data->cursors.begin() == data->cursors.active()) {
            VLOG(6) << "Adjusting default cursor (multiple): "
                    << *insert_result;
            already_applied_cursors_.set_active(insert_result);
            data->adjusted_active_cursor = true;
          }
          data->cursors.erase(data->cursors.begin());
          apply_next(apply_next);
        });
  };
  apply_next(apply_next);
  return std::move(output.value);
}

size_t CursorsTracker::Push() {
  cursors_stack_.push_back(*FindCursors(L""));
  return cursors_stack_.size();
}

size_t CursorsTracker::Pop() {
  LOG(INFO) << "CursorsTracker::Pop starts. Active: " << cursors_[L""].size()
            << ", stack: " << cursors_stack_.back().size();
  if (cursors_stack_.empty()) {
    return 0;
  }

  CHECK(&cursors_[L""] != &cursors_stack_.back());
  cursors_[L""].swap(&cursors_stack_.back());
  cursors_stack_.pop_back();

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
  if (transformation.line_delta.IsZero() &&
      transformation.column_delta.IsZero()) {
    return;
  }
  for (auto& cursors_set : cursors_) {
    AdjustCursorsSet(transformation, &cursors_set.second);
  }
  for (auto& cursors_set : cursors_stack_) {
    AdjustCursorsSet(transformation, &cursors_set);
  }
  AdjustCursorsSet(transformation, &already_applied_cursors_);
}

std::ostream& operator<<(std::ostream& os,
                         const CursorsTracker::Transformation& t) {
  os << "[range: " << t.range << ", line: " << t.line_delta
     << ", line_ge: " << t.line_lower_bound << ", column: " << t.column_delta
     << ", column_ge: " << t.column_lower_bound << ", output: " << OutputOf(t)
     << "]";
  return os;
}

bool operator==(const CursorsTracker::Transformation& a,
                const CursorsTracker::Transformation& b) {
  return a.range == b.range && a.line_delta == b.line_delta &&
         a.line_lower_bound == b.line_lower_bound &&
         a.column_delta == b.column_delta &&
         a.column_lower_bound == b.column_lower_bound;
}

}  // namespace afc::editor
