#include "src/buffer_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/dirname.h"
#include "src/line.h"
#include "src/parse_tree.h"
#include "src/terminal.h"

namespace afc {
namespace editor {
namespace {
OutputProducer::Generator LineHighlighter(OutputProducer::Generator generator) {
  return OutputProducer::Generator{
      std::nullopt, [generator]() {
        auto output = generator.generate();
        Line::Options line_options(*output.line);
        for (auto& m : line_options.modifiers) {
          auto it = m.insert(LineModifier::REVERSE);
          if (!it.second) {
            m.erase(it.first);
          }
        }
        output.line = std::make_shared<Line>(std::move(line_options));
        return output;
      }};
}
OutputProducer::Generator ParseTreeHighlighter(
    ColumnNumber begin, ColumnNumber end, OutputProducer::Generator generator) {
  return OutputProducer::Generator{
      std::nullopt, [=]() {
        OutputProducer::LineWithCursor output = generator.generate();
        Line::Options line_options(std::move(*output.line));
        LineModifierSet modifiers = {LineModifier::BLUE};
        for (auto index = begin; index <= end; ++index) {
          line_options.modifiers[index.column] = modifiers;
        }
        output.line = std::make_shared<Line>(std::move(line_options));
        return output;
      }};
}

OutputProducer::Generator ParseTreeHighlighterTokens(
    const ParseTree* root, LineColumn initial_position,
    OutputProducer::Generator generator) {
  return OutputProducer::Generator{
      std::nullopt, [root, initial_position, generator]() {
        LineColumn position = initial_position;
        OutputProducer::LineWithCursor input = generator.generate();
        Line::Options options(std::move(*input.line));
        std::vector<const ParseTree*> current = {root};

        for (size_t i = 0; i < options.modifiers.size(); i++) {
          if (i == 0 ||
              (!current.empty() && current.back()->range.end <= position)) {
            // Go up the tree until we're at a root that includes position.
            while (!current.empty() && current.back()->range.end <= position) {
              current.pop_back();
            }

            if (!current.empty()) {
              // Go down the tree. At each position, pick the first children
              // that ends after position (it may also start *after*
              // position).
              while (!current.back()->children.empty()) {
                auto it = current.back()->children.UpperBound(
                    position,
                    [](const LineColumn& position, const ParseTree& candidate) {
                      return position < candidate.range.end;
                    });
                if (it == current.back()->children.end()) {
                  break;
                }
                current.push_back(&*it);
              }
            }
          }

          if (options.modifiers[i].empty()) {
            for (auto& t : current) {
              if (t->range.Contains(position)) {
                for (auto& modifier : t->modifiers) {
                  options.modifiers[i].insert(modifier);
                }
              }
            }
          }

          ++position.column;
        }
        input.line = std::make_shared<Line>(std::move(options));
        return input;
      }};
}
}  // namespace

BufferOutputProducer::BufferOutputProducer(
    std::shared_ptr<OpenBuffer> buffer,
    std::shared_ptr<LineScrollControl::Reader> line_scroll_control_reader,
    LineNumberDelta lines_shown, ColumnNumberDelta columns_shown,
    ColumnNumber initial_column,
    std::shared_ptr<const ParseTree> zoomed_out_tree)
    : buffer_(std::move(buffer)),
      line_scroll_control_reader_(std::move(line_scroll_control_reader)),
      lines_shown_(lines_shown),
      columns_shown_(columns_shown),
      initial_column_(initial_column),
      root_(buffer_->parse_tree()),
      current_tree_(buffer_->current_tree(root_.get())),
      zoomed_out_tree_(std::move(zoomed_out_tree)) {
  CHECK(line_scroll_control_reader_ != nullptr);
  if (buffer_->Read(buffer_variables::reload_on_display)) {
    buffer_->Reload();
  }
}

OutputProducer::Generator BufferOutputProducer::Next() {
  auto optional_range = line_scroll_control_reader_->GetRange();
  if (!optional_range.has_value()) {
    return Generator::Empty();
  }

  auto range = optional_range.value();
  auto line = range.begin.line;

  if (line > buffer_->EndLine()) {
    line_scroll_control_reader_->RangeDone();
    return Generator::Empty();
  }

  std::optional<ColumnNumber> active_cursor_column;
  auto line_contents = buffer_->LineAt(line);

  Generator output{
      std::nullopt, [line_contents, range, this]() {
        Line::OutputOptions options;
        options.initial_column = range.begin.column;
        if (range.begin.line == range.end.line) {
          CHECK_GE(range.end.column, range.begin.column);
          CHECK_LE(range.end.column - range.begin.column, columns_shown_);
          options.width = range.end.column - range.begin.column;
        } else {
          options.width = columns_shown_;
        }

        if (!buffer_->Read(buffer_variables::atomic_lines)) {
          std::set<ColumnNumber> current_cursors;
          for (auto& c : line_scroll_control_reader_->GetCurrentCursors()) {
            if (LineColumn(range.begin.line, c) == buffer_->position()) {
              options.active_cursor_column = c;
            } else {
              options.inactive_cursor_columns.insert(c);
            }
          }
          options.modifiers_inactive_cursors = {
              LineModifier::REVERSE,
              buffer_->Read(buffer_variables::multiple_cursors)
                  ? LineModifier::CYAN
                  : LineModifier::BLUE};
        }

        line_scroll_control_reader_->RangeDone();
        return line_contents->Output(std::move(options));
      }};

  if (current_tree_ != root_.get() &&
      range.begin.line >= current_tree_->range.begin.line &&
      range.begin.line <= current_tree_->range.end.line) {
    ColumnNumber begin = range.begin.line == current_tree_->range.begin.line
                             ? current_tree_->range.begin.column
                             : ColumnNumber(0);
    ColumnNumber end = range.begin.line == current_tree_->range.end.line
                           ? current_tree_->range.end.column
                           : line_contents->EndColumn();
    output = ParseTreeHighlighter(begin, end, std::move(output));
  } else if (!buffer_->parse_tree()->children.empty()) {
    output =
        ParseTreeHighlighterTokens(root_.get(), range.begin, std::move(output));
  }

  CHECK(line_contents->contents() != nullptr);
  if (buffer_->Read(buffer_variables::atomic_lines) &&
      buffer_->active_cursors()->cursors_in_line(line)) {
    output = LineHighlighter(std::move(output));
  }

  return output;
}
}  // namespace editor
}  // namespace afc
