#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void HorizontalSplitOutputProducer::WriteLine(Options options) {
  CHECK_LT(current_producer_, lines_per_producer_.size());
  while (current_producer_line_ >= lines_per_producer_[current_producer_]) {
    current_producer_++;
    current_producer_line_ = 0;
    CHECK_LT(current_producer_, lines_per_producer_.size());
  }

  output_producers_[current_producer_]->WriteLine(std::move(options));
  current_producer_line_++;
}

}  // namespace editor
}  // namespace afc
