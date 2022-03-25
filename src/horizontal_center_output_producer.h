#ifndef __AFC_EDITOR HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/line_column.h"
#include "src/output_producer.h"

namespace afc::editor {

class HorizontalCenterOutputProducer : public OutputProducer {
 public:
  HorizontalCenterOutputProducer(std::unique_ptr<OutputProducer> delegate,
                                 ColumnNumberDelta width);

  Output Produce(LineNumberDelta lines) override;

 private:
  const std::unique_ptr<OutputProducer> delegate_;
  const ColumnNumberDelta width_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
