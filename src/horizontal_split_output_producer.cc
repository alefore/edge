#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace afc {
namespace editor {
OutputProducer::Generator HorizontalSplitOutputProducer::Next() {
  CHECK_LT(current_row_, rows_.size());
  while (current_row_line_.ToDelta() >= rows_[current_row_].lines) {
    current_row_++;
    current_row_line_ = LineNumber(0);
    CHECK_LT(current_row_, rows_.size());
  }

  OutputProducer::Generator delegate;
  if (rows_[current_row_].producer != nullptr) {
    delegate = rows_[current_row_].producer->Next();
  } else {
    delegate = Generator::Empty();
  }
  current_row_line_++;

  if (current_row_ != index_active_) {
    if (delegate.inputs_hash.has_value()) {
      delegate.inputs_hash = std::hash<size_t>{}(delegate.inputs_hash.value()) +
                             std::hash<size_t>{}(329ul);
    }
    delegate.generate = [generate = std::move(delegate.generate)]() {
      auto output = generate();
      output.cursor = std::nullopt;
      return output;
    };
  }
  return delegate;
}

}  // namespace editor
}  // namespace afc
