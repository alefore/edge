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

Line::Options::Options(Line line)
    : contents(std::move(line.contents())),
      modifiers(line.modifiers()),  // TODO: std::move,
      end_of_line_modifiers(line.end_of_line_modifiers()),
      environment(line.environment()) {}

ColumnNumber Line::Options::EndColumn() const {
  // TODO: Compute this separately, taking the width of characters into
  // account.
  return ColumnNumber(contents->size());
}

void Line::Options::AppendCharacter(wchar_t c, LineModifierSet modifier) {
  ValidateInvariants();
  modifiers.push_back(std::move(modifier));
  contents = StringAppend(std::move(contents), NewLazyString(wstring(1, c)));
  ValidateInvariants();
}

void Line::Options::AppendString(std::shared_ptr<LazyString> suffix) {
  AppendString(std::move(suffix), {});
}

void Line::Options::AppendString(std::shared_ptr<LazyString> suffix,
                                 LineModifierSet suffix_modifiers) {
  ValidateInvariants();
  modifiers.insert(modifiers.end(), suffix->size(), suffix_modifiers);
  contents = StringAppend(std::move(contents), std::move(suffix));
  ValidateInvariants();
}

void Line::Options::AppendString(std::wstring suffix,
                                 LineModifierSet modifiers) {
  AppendString(NewLazyString(std::move(suffix)), std::move(modifiers));
}

void Line::Options::Append(Line line) {
  ValidateInvariants();
  contents = StringAppend(std::move(contents), std::move(line.contents()));
  modifiers.insert(modifiers.end(), line.modifiers().begin(),
                   line.modifiers().end());
  end_of_line_modifiers = line.end_of_line_modifiers();
  ValidateInvariants();
}

void Line::Options::ValidateInvariants() {
  CHECK_EQ(modifiers.size(), contents->size());
}

Line::Line(wstring x) : Line(Line::Options(NewLazyString(std::move(x)))) {}

Line::Line(const Options& options)
    : environment_(options.environment == nullptr
                       ? std::make_shared<Environment>()
                       : options.environment),
      contents_(options.contents),
      options_(std::move(options)) {
  CHECK(contents_ != nullptr);
  CHECK_EQ(contents_->size(), options_.modifiers.size());
}

Line::Line(const Line& line) {
  std::unique_lock<std::mutex> lock(line.mutex_);
  environment_ = line.environment_;
  contents_ = line.contents_;
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
  CHECK_EQ(contents_->size(), options_.modifiers.size());
  contents_ = StringAppend(
      afc::editor::Substring(contents_, ColumnNumber(0), column.ToDelta()),
      afc::editor::Substring(contents_, column + delta));
  auto it = options_.modifiers.begin() + column.column;
  options_.modifiers.erase(it, it + delta.column_delta);
  CHECK_EQ(contents_->size(), options_.modifiers.size());
}

void Line::DeleteCharacters(ColumnNumber column) {
  CHECK_LE(column, EndColumn());
  DeleteCharacters(column, EndColumn() - column);
}

void Line::InsertCharacterAtPosition(ColumnNumber column) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), options_.modifiers.size());
  contents_ = StringAppend(
      StringAppend(
          afc::editor::Substring(contents_, ColumnNumber(0), column.ToDelta()),
          NewLazyString(L" ")),
      afc::editor::Substring(contents_, column));

  options_.modifiers.push_back(unordered_set<LineModifier, hash<int>>());
  for (size_t i = options_.modifiers.size() - 1; i > column.column; i--) {
    options_.modifiers[i] = options_.modifiers[i - 1];
  }
}

void Line::SetCharacter(
    ColumnNumber column, int c,
    const unordered_set<LineModifier, hash<int>>& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), options_.modifiers.size());
  shared_ptr<LazyString> str = NewLazyString(wstring(1, c));
  if (column >= EndColumnWithLock()) {
    contents_ = StringAppend(contents_, str);
    options_.modifiers.push_back(modifiers);
  } else {
    contents_ = StringAppend(
        StringAppend(afc::editor::Substring(contents_, ColumnNumber(0),
                                            column.ToDelta()),
                     str),
        afc::editor::Substring(contents_, column + ColumnNumberDelta(1)));
    if (options_.modifiers.size() <= column.column) {
      options_.modifiers.resize(column.column + 1);
    }
    options_.modifiers[column.column] = modifiers;
  }
  CHECK_EQ(contents_->size(), options_.modifiers.size());
}

void Line::SetAllModifiers(const LineModifierSet& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), options_.modifiers.size());
  options_.modifiers.assign(contents_->size(), modifiers);
  options_.end_of_line_modifiers = modifiers;
  CHECK_EQ(contents_->size(), options_.modifiers.size());
}

void Line::Append(const Line& line) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), options_.modifiers.size());
  CHECK_EQ(line.contents_->size(), line.options_.modifiers.size());
  CHECK(this != &line);
  contents_ = StringAppend(contents_, line.contents_);
  for (auto& m : line.options_.modifiers) {
    options_.modifiers.push_back(m);
  }
  options_.end_of_line_modifiers = line.options_.end_of_line_modifiers;
  CHECK_EQ(contents_->size(), options_.modifiers.size());
}

std::shared_ptr<vm::Environment> Line::environment() const {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK(environment_ != nullptr);
  return environment_;
}

OutputProducer::LineWithCursor Line::Output(
    const OutputOptions& options) const {
  CHECK(environment_ != nullptr);
  VLOG(5) << "Producing output of line: " << ToString();
  Line::Options line_output;
  ColumnNumber output_column;
  OutputProducer::LineWithCursor line_with_cursor;
  LineModifierSet current_modifiers;
  ColumnNumber input_column = options.initial_column;
  for (; input_column < EndColumnWithLock() &&
         output_column < ColumnNumber(0) + options.width;
       ++input_column) {
    wint_t c = GetWithLock(input_column);
    CHECK(c != '\n');

    // TODO: Optimize.
    if (input_column.column >= options_.modifiers.size()) {
      current_modifiers.clear();
    } else {
      current_modifiers = options_.modifiers[input_column.column];
    }

    if (options.active_cursor_column.has_value() &&
        options.active_cursor_column.value() == input_column) {
      line_with_cursor.cursor = output_column;
    } else if (options.inactive_cursor_columns.find(input_column) !=
               options.inactive_cursor_columns.end()) {
      current_modifiers = options.modifiers_inactive_cursors;
    }

    switch (c) {
      case L'\r':
        break;

      case L'\t': {
        ColumnNumber target =
            ColumnNumber(0) +
            ((output_column.ToDelta() / 8) + ColumnNumberDelta(1)) * 8;
        line_output.AppendString(
            ColumnNumberDelta::PaddingString(target - output_column, L' '),
            LineModifierSet());
        output_column = target;
        break;
      }

      default:
        VLOG(8) << "Print character: " << c;
        line_output.AppendCharacter(c, current_modifiers);
        output_column += ColumnNumberDelta(wcwidth(c));
    }
  }
  line_output.end_of_line_modifiers = input_column == EndColumnWithLock()
                                          ? options_.end_of_line_modifiers
                                          : current_modifiers;
  line_with_cursor.line = std::make_shared<Line>(std::move(line_output));
  if (!line_with_cursor.cursor.has_value() &&
      options.active_cursor_column.has_value()) {
    line_with_cursor.cursor = output_column;
  }
  return line_with_cursor;
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
