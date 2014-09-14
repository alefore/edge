#include "line.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "char_buffer.h"
#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"

namespace afc {
namespace editor {

using std::hash;
using std::unordered_set;

Line::Line(const Options& options)
    : contents_(options.contents),
      modifiers_(options.modifiers),
      modified_(false),
      filtered_(true),
      filter_version_(0) {
  assert(contents_ != nullptr);
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

void Line::InsertCharacter(int c) {
  contents_ = StringAppend(contents_, NewCopyString(string(1, c)));
  modifiers_.emplace_back();
}

void Line::SetCharacter(size_t position, int c,
                        const unordered_set<Modifier, hash<int>>& modifiers) {
  shared_ptr<LazyString> str = NewCopyString(string(1, c));
  if (position >= size()) {
    contents_ = StringAppend(contents_, str);
    modifiers_.push_back(modifiers);
  } else {
    contents_ = StringAppend(
        StringAppend(afc::editor::Substring(contents_, 0, position), str),
        afc::editor::Substring(contents_, position + 1));
    modifiers_[position] = modifiers;
  }
}

void Line::set_activate(unique_ptr<EditorMode> activate) {
  activate_ = std::move(activate);
}

void Line::Output(const EditorState*,
                  const shared_ptr<OpenBuffer>& buffer,
                  OutputReceiverInterface* receiver) {
  size_t width = receiver->width();
  size_t output_column = 0;
  size_t input_column = buffer->view_start_column();
  while (input_column < size() && output_column < width) {
    int c = get(input_column);
    assert(c != '\n');
    // TODO: Optimize.
    receiver->AddModifier(Line::RESET);
    if (input_column < modifiers_.size()) {
      for (auto it : modifiers_[input_column]) {
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
          receiver->AddString(string(new_output_column - output_column, ' '));
          output_column = new_output_column;
        }
        break;
      default:
        if (isprint(c)) {
          receiver->AddCharacter(c);
          output_column++;
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
    receiver->AddString(string(padding, ' '));
    receiver->AddCharacter(modified() ? '+' : '.');
    output_column += padding + 1;
  }

  if (output_column < width) {
    receiver->AddCharacter('\n');
  }
}

}  // namespace editor
}  // namespace afc
