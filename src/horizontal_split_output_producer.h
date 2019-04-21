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
      size_t index_active)
      : output_producers_(std::move(output_producers)),
        index_active_(index_active) {}

  size_t MinimumLines() override;
  void Produce(Options options) override;

 private:
  const std::vector<std::unique_ptr<OutputProducer>> output_producers_;
  const size_t index_active_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_HORIZONTAL_SPLIT_OUTPUT_PRODUCER_H__
