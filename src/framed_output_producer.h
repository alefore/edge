#ifndef __AFC_EDITOR_FRAMED_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_FRAMED_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class FramedOutputProducer : public OutputProducer {
 public:
  FramedOutputProducer(std::unique_ptr<OutputProducer> delegate, wstring title)
      : delegate_(std::move(delegate)), title_(std::move(title)) {}

  void Produce(Options options) override;

 private:
  const std::unique_ptr<OutputProducer> delegate_;
  const wstring title_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FRAMED_OUTPUT_PRODUCER_H__
