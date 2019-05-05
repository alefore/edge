#include "src/vertical_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc {
namespace editor {
OutputProducer::Generator VerticalSplitOutputProducer::Next() {
  std::vector<Generator> delegates;
  delegates.reserve(columns_.size());
  for (auto& c : columns_) {
    delegates.push_back(c.producer->Next());
  }

  return Generator{
      std::nullopt, [delegates, this]() {
        LineWithCursor output;
        Line::Options options;
        ColumnNumber initial_column;
        LineModifierSet current_modifiers;
        for (size_t i = 0; i < delegates.size(); i++) {
          // TODO: Consider adding an 'advance N spaces' function?
          options.AppendString(ColumnNumberDelta::PaddingString(
                                   initial_column - options.EndColumn(), L' '),
                               current_modifiers);
          current_modifiers.clear();

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
          options.Append(std::move(*column_data.line));
        }
        output.line = std::make_shared<Line>(std::move(options));
        return output;
      }};
}

}  // namespace editor
}  // namespace afc
