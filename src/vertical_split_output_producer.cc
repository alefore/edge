#include "src/vertical_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
void VerticalSplitOutputProducer::Produce(Options options) {
  int lines_index = 0;

  std::vector<size_t> lines_per_producer;
  for (size_t i = 0; i < output_producers_.size(); i++) {
    lines_per_producer.push_back(options.lines.size() /
                                 output_producers_.size());
  }

  size_t total = 0;
  for (auto& n : lines_per_producer) {
    total += n;
  }

  // TODO: Give them out more fairly.
  while (total < options.lines.size()) {
    lines_per_producer[0]++;
    total++;
  }

  for (size_t producer = 0; producer < output_producers_.size(); producer++) {
    Options sub_options;
    size_t first_line = lines_index;
    for (size_t line = 0; line < lines_per_producer[producer]; line++) {
      sub_options.lines.push_back(std::move(options.lines[lines_index++]));
    }
    std::optional<LineColumn> active_cursor;
    if (producer == index_active_) {
      sub_options.active_cursor = &active_cursor;
    }
    output_producers_[producer]->Produce(std::move(sub_options));
    if (active_cursor.has_value() && options.active_cursor != nullptr) {
      *options.active_cursor =
          LineColumn(active_cursor.value().line + first_line,
                     active_cursor.value().column);
    }
  }
}

}  // namespace editor
}  // namespace afc
