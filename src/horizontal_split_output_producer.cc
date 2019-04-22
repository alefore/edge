#include "src/horizontal_split_output_producer.h"

#include <glog/logging.h>

#include <algorithm>
#include <cctype>
#include <iostream>

#include "src/output_receiver.h"

namespace afc {
namespace editor {
size_t HorizontalSplitOutputProducer::MinimumLines() {
  size_t count = 0;
  for (auto& producer : output_producers_) {
    count += producer->MinimumLines();
  }
  return count;
}

void HorizontalSplitOutputProducer::Produce(Options options) {
  size_t lines_given = 0;

  std::vector<size_t> lines_per_producer;
  for (auto& producer : output_producers_) {
    lines_per_producer.push_back(producer->MinimumLines());
    lines_given += lines_per_producer.back();
  }

  // TODO: this could be done way faster (sort + single pass over all buffers).
  while (lines_given > options.lines.size()) {
    std::vector<size_t> indices_maximal_producers = {0};
    for (size_t i = 1; i < lines_per_producer.size(); i++) {
      size_t maximum = lines_per_producer[indices_maximal_producers.front()];
      if (maximum < lines_per_producer[i]) {
        indices_maximal_producers = {i};
      } else if (maximum == lines_per_producer[i]) {
        indices_maximal_producers.push_back(i);
      }
    }
    for (auto& i : indices_maximal_producers) {
      if (lines_given > options.lines.size()) {
        lines_given--;
        lines_per_producer[i]--;
      }
    }
  }

  if (lines_given < options.lines.size()) {
    lines_per_producer[index_active_] += options.lines.size() - lines_given;
  }

  int lines_index = 0;
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
