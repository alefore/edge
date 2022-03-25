#ifndef __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
#define __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__

#include "src/line_column.h"
#include "src/output_producer.h"

namespace afc::editor {
class SectionBracketsProducer : public OutputProducer {
 public:
  Output Produce(LineNumberDelta lines) override;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
