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
  HorizontalSplitOutputProducer(
      std::vector<std::unique_ptr<OutputProducer>> output_producers,
      std::vector<size_t> lines_per_producer, size_t index_active)
      : output_producers_(std::move(output_producers)),
        lines_per_producer_(std::move(lines_per_producer)),
        index_active_(index_active) {}

  void WriteLine(Options options) override;

 private:
  const std::vector<std::unique_ptr<OutputProducer>> output_producers_;
  const std::vector<size_t> lines_per_producer_;
  const size_t index_active_;

  size_t current_producer_ = 0;
  size_t current_producer_line_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
