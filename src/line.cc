#include "line.h"

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

using std::hash;
using std::unordered_set;

Line::Line(const Options& options)
    : environment_(options.parent_environment),
      contents_(options.contents),
      modifiers_(options.modifiers),
      modified_(false),
      filtered_(true),
      filter_version_(0) {
  CHECK(contents_ != nullptr);
}

shared_ptr<LazyString> Line::Substring(size_t pos, size_t length) {
  return afc::editor::Substring(contents_, pos, length);
}

shared_ptr<LazyString> Line::Substring(size_t pos) {
  return afc::editor::Substring(contents_, pos);
}

void Line::DeleteUntilEnd(size_t position) {
  if (position >= size()) { return; }
  contents_ = afc::editor::Substring(contents_, 0, position);
  modifiers_.resize(position);
}

void Line::DeleteCharacters(size_t position, size_t amount) {
  contents_ = StringAppend(
      Substring(0, position),
      Substring(position + amount));
  modifiers_.erase(modifiers_.begin() + position,
                   modifiers_.begin() + position + amount);
}

void Line::InsertCharacterAtPosition(size_t position) {
  CHECK_EQ(contents_->size(), modifiers_.size());
  contents_ = StringAppend(
      StringAppend(Substring(0, position),
                   NewCopyString(L" ")),
      Substring(position));

  modifiers_.push_back(unordered_set<Modifier, hash<int>>());
  for (size_t i = modifiers_.size() - 1; i > position; i--) {
    modifiers_[i] = modifiers_[i - 1];
  }
}

void Line::SetCharacter(size_t position, int c,
                        const unordered_set<Modifier, hash<int>>& modifiers) {
  shared_ptr<LazyString> str = NewCopyString(wstring(1, c));
  if (position >= size()) {
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
}

void Line::Output(const EditorState* editor_state,
                  const shared_ptr<OpenBuffer>& buffer,
                  size_t line,
                  OutputReceiverInterface* receiver,
                  size_t width) {
  VLOG(5) << "Producing output of line: " << ToString();
  size_t output_column = 0;
  size_t input_column = buffer->view_start_column();
  unordered_set<Line::Modifier, hash<int>> current_modifiers;
  while (input_column < size() && output_column < width) {
    wint_t c = get(input_column);
    CHECK(c != '\n');
    // TODO: Optimize.
    if (input_column >= modifiers_.size()) {
      receiver->AddModifier(Line::RESET);
    } else if (modifiers_[input_column] != current_modifiers) {
      receiver->AddModifier(Line::RESET);
      current_modifiers = modifiers_[input_column];
      for (auto it : current_modifiers) {
        receiver->AddModifier(it);
      }
    }
    switch (c) {
      case '\r':
        break;
      case '\t':
        {
          size_t new_output_column = min(width,
              8 * static_cast<size_t>(
                  1 + floor(static_cast<double>(output_column) / 8.0)));
          assert(new_output_column > output_column);
          assert(new_output_column - output_column <= 8);
          receiver->AddString(wstring(new_output_column - output_column, ' '));
          output_column = new_output_column;
        }
        break;
      default:
        if (iswprint(c)) {
          VLOG(8) << "Print character: " << c;
          receiver->AddCharacter(c);
          output_column++;
        } else {
          VLOG(7) << "Ignoring non-printable character: " << c;
        }
    }
    input_column++;
  }

  size_t line_width =
      buffer->read_int_variable(OpenBuffer::variable_line_width());

  if (!buffer->read_bool_variable(OpenBuffer::variable_paste_mode())
      && line_width != 0
      && buffer->view_start_column() < line_width
      && output_column <= line_width - buffer->view_start_column()
      && line_width - buffer->view_start_column() < width) {
    size_t padding = line_width - buffer->view_start_column() - output_column;
    receiver->AddString(wstring(padding, L' '));

    auto all_marks = buffer->GetLineMarks(*editor_state);
    auto marks = all_marks->equal_range(line);

    char info_char = '.';
    if (marks.first != marks.second) {
      receiver->AddModifier(Modifier::RED);
      receiver->AddModifier(Modifier::BOLD);
      info_char = '!';
    } else if (modified()) {
      receiver->AddModifier(Modifier::GREEN);
      info_char = '.';
    } else {
      receiver->AddModifier(Modifier::DIM);
    }
    receiver->AddCharacter(info_char);
    receiver->AddModifier(Modifier::RESET);
    output_column += padding + 1;
  }

  if (output_column < width) {
    receiver->AddModifier(Line::RESET);
    VLOG(6) << "Adding newline characters.";
    receiver->AddString(L"\n");
  }
}

OutputReceiverOptimizer::~OutputReceiverOptimizer() {
  Flush();
}

void OutputReceiverOptimizer::AddCharacter(wchar_t character) {
  buffer_.push_back(character);
}

void OutputReceiverOptimizer::AddString(const wstring& str) {
  buffer_.append(str);
}

void OutputReceiverOptimizer::AddModifier(Line::Modifier modifier) {
  if (modifier == Line::RESET) {
    if (modifiers_.empty()) {
      DVLOG(5) << "That was easy: reset, but modifiers were already empty.";
      return;
    }

    Flush();
    modifiers_.clear();
    return;
  }

  if (modifiers_.find(modifier) != modifiers_.end()) {
    DVLOG(5) << "That was easy: Modifier was already present.";
    return;
  }

  Flush();
  modifiers_.insert(modifier);
}

void OutputReceiverOptimizer::Flush() {
  DCHECK(modifiers_.find(Line::RESET) == modifiers_.end());
  DCHECK(last_modifiers_.find(Line::RESET) == last_modifiers_.end());

  if (!std::includes(modifiers_.begin(), modifiers_.end(),
                     last_modifiers_.begin(), last_modifiers_.end())) {
    DVLOG(5) << "Last modifiers not contained in new modifiers.";
    delegate_->AddModifier(Line::RESET);
    last_modifiers_.clear();
  }

  for (auto& modifier : modifiers_) {
    auto inserted = last_modifiers_.insert(modifier).second;
    if (inserted) {
      delegate_->AddModifier(modifier);
    }
  }
  DCHECK(last_modifiers_ == modifiers_);

  if (!buffer_.empty()) {
    delegate_->AddString(buffer_);
    buffer_.clear();
  }
}

}  // namespace editor
}  // namespace afc
