#include "src/widget_list.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <ctgmath>
#include <iostream>
#include <numeric>

#include "src/buffer.h"
#include "src/buffer_output_producer.h"
#include "src/buffer_variables.h"
#include "src/buffer_widget.h"
#include "src/buffers_list.h"
#include "src/char_buffer.h"
#include "src/columns_vector.h"
#include "src/editor.h"
#include "src/editor_variables.h"
#include "src/frame_output_producer.h"
#include "src/rows_vector.h"
#include "src/widget.h"
#include "src/wstring.h"

namespace afc {
namespace editor {

WidgetList::WidgetList(std::vector<std::unique_ptr<Widget>> children,
                       size_t active)
    : children_(std::move(children)), active_(active) {}

WidgetList::WidgetList(std::unique_ptr<Widget> children)
    : WidgetList(
          [&]() {
            std::vector<std::unique_ptr<Widget>> output;
            output.push_back(std::move(children));
            return output;
          }(),
          0) {}

WidgetListHorizontal::WidgetListHorizontal(std::unique_ptr<Widget> children)
    : WidgetList(
          [&]() {
            std::vector<std::unique_ptr<Widget>> output;
            output.push_back(std::move(children));
            return output;
          }(),
          0) {}

WidgetListHorizontal::WidgetListHorizontal(
    std::vector<std::unique_ptr<Widget>> children, size_t active)
    : WidgetList(std::move(children), active) {}

LineWithCursor::Generator::Vector WidgetListHorizontal::CreateOutput(
    OutputProducerOptions options) const {
  if (options.size.line.IsZero()) return LineWithCursor::Generator::Vector{};

  std::vector<LineNumberDelta> lines_per_child;
  for (auto& child : children_) {
    lines_per_child.push_back(child->MinimumLines());
  }

  LineNumberDelta lines_given = std::accumulate(
      lines_per_child.begin(), lines_per_child.end(), LineNumberDelta(0));

  // The total number of lines to give to all children. Will exclude additional
  // kInformationLines when children are skipped.
  auto lines_available = options.size.line;

  // TODO: this could be done way faster (sort + single pass over all
  // buffers).
  if (lines_given > options.size.line) {
    static const LineNumberDelta kInformationLines = LineNumberDelta(1);
    lines_available -= kInformationLines;
    while (lines_given > lines_available) {
      size_t index_maximal_lines = 0;
      for (size_t i = 1; i < lines_per_child.size(); i++) {
        LineNumberDelta maximum = lines_per_child[index_maximal_lines];
        if (maximum < lines_per_child[i] ||
            (index_maximal_lines == active_ && !lines_per_child[i].IsZero())) {
          index_maximal_lines = i;
        }
      }

      CHECK_LT(index_maximal_lines, lines_per_child.size());
      if (lines_given == lines_per_child[index_maximal_lines]) {
        // This child is the only child receiving any lines.
        lines_given = lines_available;
        lines_per_child[index_maximal_lines] = lines_available;
        continue;
      }

      lines_given -= lines_per_child[index_maximal_lines];
      lines_per_child[index_maximal_lines] = LineNumberDelta();
    }
  }

  CHECK_EQ(lines_given,
           std::accumulate(lines_per_child.begin(), lines_per_child.end(),
                           LineNumberDelta(0)));

  if (lines_given.IsZero()) return LineWithCursor::Generator::Vector{};

  size_t children_skipped = std::count(
      lines_per_child.begin(), lines_per_child.end(), LineNumberDelta());

  bool expand_beyond_desired = false;
  while (lines_available > lines_given && !lines_per_child.empty()) {
    std::set<size_t> indices_minimal;  // Only includes those who can grow.
    for (size_t i = 0; i < lines_per_child.size(); i++) {
      if (!expand_beyond_desired &&
          lines_per_child[i] >= children_[i]->DesiredLines())
        continue;
      if (!indices_minimal.empty()) {
        auto minimal = lines_per_child[*indices_minimal.begin()];
        if (minimal < lines_per_child[i]) {
          continue;
        } else if (lines_per_child[i] < minimal) {
          indices_minimal.clear();
        }
      }
      indices_minimal.insert(i);
    }
    if (indices_minimal.empty()) {
      CHECK(!expand_beyond_desired);
      expand_beyond_desired = true;
      continue;
    }
    for (auto& i : indices_minimal) {
      if (lines_available == lines_given) break;
      ++lines_per_child[i];
      ++lines_given;
    }
  }

  CHECK_EQ(lines_available,
           std::accumulate(lines_per_child.begin(), lines_per_child.end(),
                           LineNumberDelta(0)));

  LineWithCursor::Generator::Vector output;
  CHECK_EQ(children_.size(), lines_per_child.size());
  for (size_t index = 0; index < children_.size(); index++) {
    LineWithCursor::Generator::Vector child_lines =
        GetChildOutput(options, index, lines_per_child[index]);
    CHECK_EQ(child_lines.size(), lines_per_child[index]);
    output = AppendRows(std::move(output), std::move(child_lines),
                        index == active_ ? 1 : 0);
  }

  if (children_skipped > 0) {
    output.lines.push_back(
        {.inputs_hash = {}, .generate = [children_skipped] {
           return LineWithCursor(FrameLine(
               {.title =
                    L"Additional files: " + std::to_wstring(children_skipped),
                .active_state =
                    FrameOutputProducerOptions::ActiveState::kActive}));
         }});
  }

  return output;
}

LineWithCursor::Generator::Vector WidgetListHorizontal::GetChildOutput(
    OutputProducerOptions options, size_t index, LineNumberDelta lines) const {
  options.size.line = lines;
  return options.size.line.IsZero()
             ? LineWithCursor::Generator::Vector{}
             : children_[index].get()->CreateOutput(options);
}

LineNumberDelta WidgetListHorizontal::MinimumLines() const {
  LineNumberDelta count;
  for (auto& child : children_) {
    count += child->MinimumLines();
  }
  return count;
}

LineNumberDelta WidgetListHorizontal::DesiredLines() const {
  LineNumberDelta count;
  for (auto& child : children_) {
    count += child->DesiredLines();
  }
  return count;
}

WidgetListVertical::WidgetListVertical(std::unique_ptr<Widget> children)
    : WidgetList(std::move(children)) {}

WidgetListVertical::WidgetListVertical(
    std::vector<std::unique_ptr<Widget>> children, size_t active)
    : WidgetList(std::move(children), active) {}

LineWithCursor::Generator::Vector WidgetListVertical::CreateOutput(
    OutputProducerOptions options) const {
  ColumnsVector columns_vector{.index_active = active_};
  columns_vector.columns.resize(children_.size());

  ColumnNumberDelta base_columns = options.size.column / children_.size();
  ColumnNumberDelta columns_left =
      options.size.column - base_columns * children_.size();
  CHECK_LT(columns_left, ColumnNumberDelta(children_.size()));
  for (auto& column : columns_vector.columns) {
    column.width = base_columns;
    if (columns_left > ColumnNumberDelta(0)) {
      ++*column.width;
      --columns_left;
    }
  }
  CHECK_EQ(columns_left, ColumnNumberDelta(0));

  for (size_t index = 0; index < children_.size(); index++) {
    auto& column = columns_vector.columns[index];
    OutputProducerOptions child_options = options;
    CHECK(column.width.has_value());
    child_options.size.column = column.width.value();
    child_options.main_cursor_behavior =
        index == active_
            ? options.main_cursor_behavior
            : Widget::OutputProducerOptions::MainCursorBehavior::kHighlight;
    column.lines = children_[index]->CreateOutput(std::move(child_options));
  }
  return columns_vector.columns.empty()
             ? RepeatLine(LineWithCursor(Line()), options.size.line)
             : OutputFromColumnsVector(std::move(columns_vector));
}

LineNumberDelta WidgetListVertical::MinimumLines() const {
  LineNumberDelta output = children_[0]->MinimumLines();
  for (auto& child : children_) {
    output = max(child->MinimumLines(), output);
  }
  static const LineNumberDelta kFrameLines = LineNumberDelta(1);
  return output + kFrameLines;
}

LineNumberDelta WidgetListVertical::DesiredLines() const {
  LineNumberDelta output = children_[0]->DesiredLines();
  for (auto& child : children_) {
    output = max(child->DesiredLines(), output);
  }
  static const LineNumberDelta kFrameLines = LineNumberDelta(1);
  return output + kFrameLines;
}

}  // namespace editor
}  // namespace afc
