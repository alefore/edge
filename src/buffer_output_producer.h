#ifndef __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class BufferOutputProducer : public OutputProducer {
 public:
  BufferOutputProducer(std::shared_ptr<OpenBuffer> buffer)
      : buffer_(std::move(buffer)) {}

  size_t MinimumLines() override;

  void Produce(Options options) override;

 private:
  const std::shared_ptr<OpenBuffer> buffer_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
