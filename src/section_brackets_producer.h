#ifndef __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
#define __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__

#include "src/line_column.h"
#include "src/output_producer.h"

namespace afc::editor {
LineWithCursor::Generator::Vector SectionBrackets(LineNumberDelta lines);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SECTION_BRACKETS_PRODUCER_H__
