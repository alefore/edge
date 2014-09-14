#include "line.h"

#include <cassert>
#include <cmath>

#include "editor.h"
#include "substring.h"

namespace afc {
namespace editor {

Line::Line(const Options& options)
    : contents_(options.contents),
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
