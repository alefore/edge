#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void HorizontalSplitOutputProducer::WriteLine(Options options) {
  CHECK_LT(current_row_, rows_.size());
  while (current_row_line_.ToDelta() >= rows_[current_row_].lines) {
    current_row_++;
    current_row_line_ = LineNumber(0);
    CHECK_LT(current_row_, rows_.size());
  }

  if (current_row_ != index_active_) {
    options.active_cursor = nullptr;
  }
  if (rows_[current_row_].producer != nullptr) {
    rows_[current_row_].producer->WriteLine(std::move(options));
  }
  current_row_line_++;
}

}  // namespace editor
}  // namespace afc
