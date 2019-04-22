#include "src/buffer_widget.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
size_t GetCurrentColumn(OpenBuffer* buffer) {
  if (buffer->lines_size() == 0) {
    return 0;
  } else if (buffer->position().line >= buffer->lines_size()) {
    return buffer->contents()->back()->size();
  } else if (!buffer->IsLineFiltered(buffer->position().line)) {
    return 0;
  } else {
    return min(buffer->position().column,
               buffer->LineAt(buffer->position().line)->size());
  }
}

size_t GetDesiredViewStartColumn(OpenBuffer* buffer) {
  if (buffer->Read(buffer_variables::wrap_long_lines())) {
    return 0;
  }
  size_t effective_size = 80;  // TODO: This is bogus.
  effective_size -=
      min(effective_size, /* GetInitialPrefixSize(*buffer) */ 3ul);
  size_t column = GetCurrentColumn(buffer);
  return column - min(column, effective_size);
}
}  // namespace

BufferWidget::BufferWidget(ConstructorAccessTag,
                           std::weak_ptr<OpenBuffer> buffer)
    : leaf_(buffer) {}

/* static */
std::unique_ptr<BufferWidget> BufferWidget::New(
    std::weak_ptr<OpenBuffer> buffer) {
  return std::make_unique<BufferWidget>(ConstructorAccessTag(), buffer);
}

wstring BufferWidget::Name() const {
  auto buffer = Lock();
  return buffer == nullptr ? L"" : buffer->Read(buffer_variables::name());
}

wstring BufferWidget::ToString() const {
  auto buffer = leaf_.lock();
  return L"[buffer tree leaf" +
         (buffer == nullptr ? L"nullptr"
                            : buffer->Read(buffer_variables::name())) +
         L"]";
}

BufferWidget* BufferWidget::GetActiveLeaf() { return this; }

std::unique_ptr<OutputProducer> BufferWidget::CreateOutputProducer() {
  auto buffer = Lock();
  if (buffer == nullptr) {
    return nullptr;
  }
  return std::make_unique<BufferOutputProducer>(buffer, lines_, view_start_);
}

void BufferWidget::SetLines(size_t lines) {
  lines_ = lines;
  auto buffer = leaf_.lock();
  if (buffer == nullptr) {
    return;
  }
  buffer->set_lines_for_zoomed_out_tree(lines);

  size_t line = min(buffer->position().line, buffer->contents()->size() - 1);
  size_t margin_lines = min(
      lines_ / 2 - 1,
      max(static_cast<size_t>(ceil(
              buffer->Read(buffer_variables::margin_lines_ratio()) * lines_)),
          static_cast<size_t>(
              max(buffer->Read(buffer_variables::margin_lines()), 0))));

  if (view_start_.line > line - min(margin_lines, line) &&
      (buffer->child_pid() != -1 || buffer->fd() == -1)) {
    view_start_.line = line - min(margin_lines, line);
    // editor_state->ScheduleRedraw();
  } else if (view_start_.line + lines_ <=
             min(buffer->lines_size(), line + margin_lines)) {
    view_start_.line =
        min(buffer->lines_size() - 1, line + margin_lines) - lines_ + 1;
    // editor_state->ScheduleRedraw();
  }

  view_start_.column = GetDesiredViewStartColumn(buffer.get());
}

size_t BufferWidget::lines() const { return lines_; }

size_t BufferWidget::MinimumLines() {
  auto buffer = Lock();
  return buffer == nullptr
             ? 0
             : max(0,
                   buffer->Read(buffer_variables::buffer_list_context_lines()));
}

LineColumn BufferWidget::view_start() const { return view_start_; }

std::shared_ptr<OpenBuffer> BufferWidget::Lock() const { return leaf_.lock(); }

void BufferWidget::SetBuffer(std::weak_ptr<OpenBuffer> buffer) {
  leaf_ = std::move(buffer);
  SetLines(lines_);  // Causes things to be recomputed.
}

}  // namespace editor
}  // namespace afc
