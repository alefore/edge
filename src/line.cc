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

shared_ptr<LazyString> Line::Substring(size_t pos, size_t length) const {
  return afc::editor::Substring(contents(), pos, length);
}

shared_ptr<LazyString> Line::Substring(size_t pos) const {
  return afc::editor::Substring(contents(), pos);
}

void Line::DeleteCharacters(size_t position, size_t amount) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_LE(position, contents_->size());
  CHECK_LE(position + amount, contents_->size());
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ =
      StringAppend(afc::editor::Substring(contents_, 0, position),
                   afc::editor::Substring(contents_, position + amount));
  auto it = modifiers_.begin() + position;
  modifiers_.erase(it, it + amount);
  CHECK_EQ(contents_->size(), modifiers_.size());
}

void Line::DeleteCharacters(size_t position) {
  CHECK_LE(position, size());
  DeleteCharacters(position, size() - position);
}

void Line::InsertCharacterAtPosition(size_t position) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ =
      StringAppend(StringAppend(afc::editor::Substring(contents_, 0, position),
                                NewLazyString(L" ")),
                   afc::editor::Substring(contents_, position));

  modifiers_.push_back(unordered_set<LineModifier, hash<int>>());
  for (size_t i = modifiers_.size() - 1; i > position; i--) {
    modifiers_[i] = modifiers_[i - 1];
  }
}

void Line::SetCharacter(
    size_t position, int c,
    const unordered_set<LineModifier, hash<int>>& modifiers) {
  std::unique_lock<std::mutex> lock(mutex_);
  CHECK_EQ(contents_->size(), modifiers_.size());
  shared_ptr<LazyString> str = NewLazyString(wstring(1, c));
  if (position >= contents_->size()) {
    contents_ = StringAppend(contents_, str);
    modifiers_.push_back(modifiers);
  } else {
    contents_ = StringAppend(
        StringAppend(afc::editor::Substring(contents_, 0, position), str),
        afc::editor::Substring(contents_, position + 1));
    if (modifiers_.size() <= position) {
      modifiers_.resize(position + 1);
    }
    modifiers_[position] = modifiers;
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
  size_t input_column = options.position.column;
  unordered_set<LineModifier, hash<int>> current_modifiers;

  CHECK(environment_ != nullptr);

  const size_t width = options.output_receiver->width();
  while (input_column < contents_->size() &&
         options.output_receiver->column() < width) {
    wint_t c = contents_->get(input_column);
    CHECK(c != '\n');
    // TODO: Optimize.
    if (input_column >= modifiers_.size()) {
      options.output_receiver->AddModifier(LineModifier::RESET);
    } else if (modifiers_[input_column] != current_modifiers) {
      options.output_receiver->AddModifier(LineModifier::RESET);
      current_modifiers = modifiers_[input_column];
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
}

}  // namespace editor
}  // namespace afc
