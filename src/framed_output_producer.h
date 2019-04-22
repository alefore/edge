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
  FramedOutputProducer(std::unique_ptr<OutputProducer> delegate, wstring title,
                       std::optional<size_t> position_in_parent)
      : delegate_(std::move(delegate)),
        title_(std::move(title)),
        position_in_parent_(position_in_parent) {}

  void WriteLine(Options options) override;

 private:
  void AddFirstLine(const OutputProducer::Options& options);

  const std::unique_ptr<OutputProducer> delegate_;
  const wstring title_;
  const std::optional<size_t> position_in_parent_;

  int lines_written_ = 0;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FRAMED_OUTPUT_PRODUCER_H__
