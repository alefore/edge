#ifndef __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc::editor {

// Create an OutputProducer that stitches together multiple columns.
class VerticalSplitOutputProducer : public OutputProducer {
 public:
  struct Column {
    LineWithCursor::Generator::Vector lines;

    // If absent, this column will be the last column produced, and it will be
    // allowed to span the entire screen.
    std::optional<ColumnNumberDelta> width = std::nullopt;
  };

  VerticalSplitOutputProducer(std::vector<Column> columns, size_t index_active);

  LineWithCursor::Generator::Vector Produce(LineNumberDelta lines) override;

 private:
  const std::shared_ptr<const std::vector<Column>> columns_;
  const size_t index_active_;
};

}  // namespace afc::editor
#endif  // __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__
