#ifndef __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/line_column.h"
#include "src/output_producer.h"

namespace afc::editor {

class HorizontalCenterOutputProducer : public OutputProducer {
 public:
  HorizontalCenterOutputProducer(LineWithCursor::Generator::Vector lines,
                                 ColumnNumberDelta width);

  LineWithCursor::Generator::Vector Produce(LineNumberDelta lines) override;

 private:
  const LineWithCursor::Generator::Vector lines_;
  const ColumnNumberDelta width_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_HORIZONTAL_CENTER_OUTPUT_PRODUCER_H__
