#include "src/vertical_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc::editor {
namespace {
std::optional<size_t> CombineHashes(
    const std::vector<OutputProducer::Generator>& delegates) {
  size_t value = std::hash<size_t>{}(4);
  for (auto& delegate : delegates) {
    if (!delegate.inputs_hash.has_value()) {
      return std::nullopt;
    }
    value = std::hash<size_t>{}(value ^ delegate.inputs_hash.value());
  }
  return value;
}
}  // namespace

VerticalSplitOutputProducer::VerticalSplitOutputProducer(
    std::vector<Column> columns, size_t index_active)
    : columns_(std::make_shared<std::vector<Column>>(std::move(columns))),
      index_active_(index_active) {
  for (const auto& c : *columns_) {
    CHECK(c.producer != nullptr);
  }
}

OutputProducer::Output VerticalSplitOutputProducer::Produce(
    LineNumberDelta lines) {
  std::vector<Output> inputs_by_column;
  for (auto& c : *columns_) {
    Output input = c.producer->Produce(lines);
    input.lines.resize(lines.line_delta, Generator::Empty());
    inputs_by_column.push_back(std::move(input));
  }

  Output output;
  for (size_t i = 0; i < columns_->size(); i++) {
    const Column& column = columns_->at(i);
    if (column.width.has_value()) {
      output.width += *column.width;
    } else {
      output.width += inputs_by_column[i].width;
      break;  // This is the last column.
    }
  }

  // Outer index is the line being produced; inner index is the column.
  std::vector<std::vector<Generator>> generator_by_line_column(
      lines.line_delta);
  for (Output& input : inputs_by_column) {
    for (LineNumberDelta i; i < input.size(); ++i) {
      generator_by_line_column[i.line_delta].push_back(
          std::move(input.lines[i.line_delta]));
    }
  }

  for (auto& line_input : generator_by_line_column) {
    output.lines.push_back(Generator{
        .inputs_hash = CombineHashes(line_input),
        .generate = [line_input = std::move(line_input),
                     index_active = index_active_, columns = columns_]() {
          LineWithCursor output;
          Line::Options options;
          ColumnNumber initial_column;
          LineModifierSet current_modifiers;
          // This takes wide characters into account (i.e., it may differ from
          // options.EndColumn() when there are wide characters).
          ColumnNumber columns_shown;
          for (size_t i = 0; i < line_input.size(); i++) {
            options.AppendString(ColumnNumberDelta::PaddingString(
                                     initial_column - columns_shown, L' '),
                                 current_modifiers);

            LineWithCursor column_data = line_input[i].generate();
            if (column_data.cursor.has_value() && i == index_active) {
              output.cursor =
                  initial_column + column_data.cursor.value().ToDelta();
            }

            current_modifiers = column_data.line->end_of_line_modifiers();

            CHECK(column_data.line != nullptr);
            if (columns->at(i).width.has_value()) {
              // TODO: respect columns_[i].width.
              initial_column += columns->at(i).width.value();
            } else {
              i = line_input.size();  // Stop the iteration.
            }
            auto str = column_data.line->ToString();
            columns_shown += ColumnNumberDelta(
                std::max(0, wcswidth(str.c_str(), str.size())));
            options.Append(std::move(*column_data.line));
          }
          output.line = std::make_shared<Line>(std::move(options));
          return output;
        }});
  }
  return output;
}

}  // namespace afc::editor
