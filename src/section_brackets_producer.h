#ifndef __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
#define __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__

#include "src/line_column.h"
#include "src/output_producer.h"

namespace afc::editor {
class SectionBracketsProducer : public OutputProducer {
 public:
  SectionBracketsProducer(LineNumberDelta lines);
  Generator Next() override;

 private:
  const LineNumberDelta lines_;
  LineNumber current_line_;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
