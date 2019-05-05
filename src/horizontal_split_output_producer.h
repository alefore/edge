#ifndef __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class HorizontalSplitOutputProducer : public OutputProducer {
 public:
  struct Row {
    std::unique_ptr<OutputProducer> producer;
    LineNumberDelta lines;
  };

  HorizontalSplitOutputProducer(std::vector<Row> rows, size_t index_active)
      : rows_(std::move(rows)), index_active_(index_active) {}

  Generator Next() override;

 private:
  const std::vector<Row> rows_;
  const size_t index_active_;

  size_t current_row_ = 0;
  LineNumber current_row_line_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
