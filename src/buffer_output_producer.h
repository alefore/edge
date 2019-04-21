#ifndef __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class BufferOutputProducer : public OutputProducer {
 public:
  BufferOutputProducer(OpenBuffer* buffer) : buffer_(buffer) {}

  size_t MinimumLines() override;

  void Produce(Options options) override;

 private:
  OpenBuffer* const buffer_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_BUFFER_OUTPUT_PRODUCER_H__
