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
    enum class OverlapBehavior {
      // Rows after this one are pushed down in the output, unmodified.
      kSolid,
      // Each line from this row consumes entries from subsequent rows.
      kFloat
    };
    OverlapBehavior overlap_behavior = OverlapBehavior::kSolid;
  };

  HorizontalSplitOutputProducer(std::vector<Row> rows, size_t index_active)
      : rows_(std::move(rows)),
        index_active_(index_active),
        row_line_(rows_.size()) {}

  Generator Next() override;

 private:
  void ConsumeLine(size_t row);

  const std::vector<Row> rows_;
  const size_t index_active_;

  size_t current_row_ = 0;
  std::vector<LineNumber> row_line_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
