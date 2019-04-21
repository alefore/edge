#ifndef __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class VerticalSplitOutputProducer : public OutputProducer {
 public:
  VerticalSplitOutputProducer(
      std::vector<std::unique_ptr<OutputProducer>> output_producers,
      size_t index_active)
      : output_producers_(std::move(output_producers)),
        index_active_(index_active) {}

  void Produce(Options options) override;

 private:
  const std::vector<std::unique_ptr<OutputProducer>> output_producers_;
  const size_t index_active_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_VERTICAL_SPLIT_OUTPUT_PRODUCER_H__
