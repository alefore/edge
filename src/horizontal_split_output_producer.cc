#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc {
namespace editor {
OutputProducer::Generator HorizontalSplitOutputProducer::Next() {
  CHECK_EQ(row_line_.size(), rows_.size());
  while (current_row_ < rows_.size() &&
         row_line_[current_row_].ToDelta() >= rows_[current_row_].lines) {
    current_row_++;
  }
  if (current_row_ >= rows_.size()) {
    return OutputProducer::Generator::Empty();
  }

  OutputProducer::Generator delegate;
  if (rows_[current_row_].producer != nullptr) {
    delegate = rows_[current_row_].producer->Next();
  } else {
    delegate = Generator::Empty();
  }
  row_line_[current_row_]++;

  if (rows_[current_row_].overlap_behavior == Row::OverlapBehavior::kFloat) {
    ConsumeLine(current_row_ + 1);
  }

  if (current_row_ != index_active_) {
    if (delegate.inputs_hash.has_value()) {
      delegate.inputs_hash = std::hash<size_t>{}(delegate.inputs_hash.value()) +
                             std::hash<size_t>{}(329ul);
    }
    delegate.generate = [generate = std::move(delegate.generate)] {
      auto output = generate();
      output.cursor = std::nullopt;
      return output;
    };
  }
  return delegate;
}

void HorizontalSplitOutputProducer::ConsumeLine(size_t row) {
  while (row < rows_.size()) {
    if (row_line_[row].ToDelta() < rows_[row].lines) {
      ++row_line_[row];
      if (rows_[row].producer != nullptr)
        rows_[row].producer->Next();  // Skip the line.
      if (rows_[row].overlap_behavior == Row::OverlapBehavior::kSolid) return;
    }
    row++;
  }
}

}  // namespace editor
}  // namespace afc
