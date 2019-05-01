#include "src/line.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/substring.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

using std::hash;
using std::unordered_set;
using std::wstring;

Line::Line(wstring x) : Line(Line::Options(NewLazyString(std::move(x)))) {}

Line::Line(const Options& options)
    : environment_(options.environment == nullptr
                       ? std::make_shared<Environment>()
                       : options.environment),
      contents_(options.contents),
      modifiers_(options.modifiers),
      options_(std::move(options)) {
  CHECK(contents_ != nullptr);
  CHECK_EQ(contents_->size(), modifiers_.size());
}

Line::Line(const Line& line) {
  std::unique_lock<std::mutex> lock(line.mutex_);
  environment_ = line.environment_;
  contents_ = line.contents_;
  modifiers_ = line.modifiers_;
  options_ = line.options_;
}

ColumnNumber Line::EndColumn() const {
  std::unique_lock<std::mutex> lock(mutex_);
  return EndColumnWithLock();
}

wint_t Line::get(ColumnNumber column) const {
  std::unique_lock<std::mutex> lock(mutex_);
  return GetWithLock(column);
}

shared_ptr<LazyString> Line::Substring(ColumnNumber column,
                                       ColumnNumberDelta delta) const {
  return afc::editor::Substring(contents(), column, delta);
}

shared_ptr<LazyString> Line::Substring(ColumnNumber column) const {
  return afc::editor::Substring(contents(), column);
}

void Line::DeleteCharacters(ColumnNumber column, ColumnNumberDelta delta) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_LE(column, EndColumnWithLock());
  CHECK_LE(column + delta, EndColumnWithLock());
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ = StringAppend(
      afc::editor::Substring(contents_, ColumnNumber(0), column.ToDelta()),
      afc::editor::Substring(contents_, column + delta));
  auto it = modifiers_.begin() + column.column;
  modifiers_.erase(it, it + delta.value);
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::DeleteCharacters(ColumnNumber column) {
  CHECK_LE(column, EndColumn());
  DeleteCharacters(column, EndColumn() - column);
}

void Line::InsertCharacterAtPosition(ColumnNumber column) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ = StringAppend(
      StringAppend(
          afc::editor::Substring(contents_, ColumnNumber(0), column.ToDelta()),
          NewLazyString(L" ")),
      afc::editor::Substring(contents_, column));

  modifiers_.push_back(unordered_set<LineModifier, hash<int>>());
  for (size_t i = modifiers_.size() - 1; i > column.column; i--) {
    modifiers_[i] = modifiers_[i - 1];
  }
}

void Line::SetCharacter(
    ColumnNumber column, int c,
    const unordered_set<LineModifier, hash<int>>& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  shared_ptr<LazyString> str = NewLazyString(wstring(1, c));
  if (column >= EndColumnWithLock()) {
    contents_ = StringAppend(contents_, str);
    modifiers_.push_back(modifiers);
  } else {
    contents_ = StringAppend(
        StringAppend(afc::editor::Substring(contents_, ColumnNumber(0),
                                            column.ToDelta()),
                     str),
        afc::editor::Substring(contents_, column + ColumnNumberDelta(1)));
    if (modifiers_.size() <= column.column) {
      modifiers_.resize(column.column + 1);
    }
    modifiers_[column.column] = modifiers;
  }
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::SetAllModifiers(const LineModifierSet& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  modifiers_.assign(contents_->size(), modifiers);
  options_.end_of_line_modifiers = modifiers;
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::Append(const Line& line) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  CHECK_EQ(line.contents_->size(), line.modifiers_.size());
  CHECK(this != &line);
  contents_ = StringAppend(contents_, line.contents_);
  for (auto& m : line.modifiers_) {
    modifiers_.push_back(m);
  }
  options_.end_of_line_modifiers = line.options_.end_of_line_modifiers;
  CHECK_EQ(contents_->size(), modifiers_.size());
}

std::shared_ptr<vm::Environment> Line::environment() const {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK(environment_ != nullptr);
  return environment_;
}

void Line::Output(const Line::OutputOptions& options) const {
  std::unique_lock<std::mutex> lock(mutex_);
  VLOG(5) << "Producing output of line: " << ToString();
  ColumnNumber input_column = options.initial_column;
  unordered_set<LineModifier, hash<int>> current_modifiers;

  CHECK(environment_ != nullptr);

  while (input_column < EndColumnWithLock() &&
         options.output_receiver->column() < ColumnNumber(0) + options.width) {
    wint_t c = GetWithLock(input_column);
    CHECK(c != '\n');
    // TODO: Optimize.
    if (input_column.column >= modifiers_.size()) {
      options.output_receiver->AddModifier(LineModifier::RESET);
    } else if (modifiers_[input_column.column] != current_modifiers) {
      options.output_receiver->AddModifier(LineModifier::RESET);
      current_modifiers = modifiers_[input_column.column];
      for (auto it : current_modifiers) {
        options.output_receiver->AddModifier(it);
      }
    }
    switch (c) {
      case L'\r':
        break;
      default:
        VLOG(8) << "Print character: " << c;
        options.output_receiver->AddCharacter(c);
    }
    input_column++;
  }
  options.output_receiver->AddModifier(LineModifier::RESET);
  for (auto& c : options_.end_of_line_modifiers) {
    options.output_receiver->AddModifier(c);
  }
}

ColumnNumber Line::EndColumnWithLock() const {
  return ColumnNumber(contents_->size());
}

wint_t Line::GetWithLock(ColumnNumber column) const {
  CHECK_LT(column, EndColumnWithLock());
  return contents_->get(column.column);
}

}  // namespace editor
}  // namespace afc
