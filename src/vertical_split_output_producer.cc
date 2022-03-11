#include "src/vertical_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc::editor {
namespace {
std::optional<size_t> CombineHashesFromDelegates(
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
    : columns_(std::move(columns)), index_active_(index_active) {
  for (const auto& c : columns_) {
    CHECK(c.producer != nullptr);
  }
}

OutputProducer::Generator VerticalSplitOutputProducer::Next() {
  std::vector<Generator> delegates;
  delegates.reserve(columns_.size());
  for (auto& c : columns_) {
    delegates.push_back(c.producer->Next());
  }

  std::optional<size_t> hash = CombineHashesFromDelegates(delegates);
  return Generator{
      hash, [delegates = std::move(delegates), this]() {
        LineWithCursor output;
        Line::Options options;
        ColumnNumber initial_column;
        LineModifierSet current_modifiers;
        // This takes wide characters into account (i.e., it may differ from
        // options.EndColumn() when there are wide characters).
        ColumnNumber columns_shown;
        for (size_t i = 0; i < delegates.size(); i++) {
          options.AppendString(ColumnNumberDelta::PaddingString(
                                   initial_column - columns_shown, L' '),
                               current_modifiers);

          LineWithCursor column_data = delegates[i].generate();
          if (column_data.cursor.has_value() && i == index_active_) {
            output.cursor =
                initial_column + column_data.cursor.value().ToDelta();
          }

          current_modifiers = column_data.line->end_of_line_modifiers();

          CHECK(column_data.line != nullptr);
          if (columns_[i].width.has_value()) {
            // TODO: respect columns_[i].width.
            initial_column += columns_[i].width.value();
          } else {
            i = delegates.size();  // Stop the iteration.
          }
          auto str = column_data.line->ToString();
          columns_shown +=
              ColumnNumberDelta(std::max(0, wcswidth(str.c_str(), str.size())));
          options.Append(std::move(*column_data.line));
        }
        output.line = std::make_shared<Line>(std::move(options));
        return output;
      }};
}

}  // namespace afc::editor
