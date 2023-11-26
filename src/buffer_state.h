#ifndef __AFC_EDITOR_BUFFER_STATE_H__
#define __AFC_EDITOR_BUFFER_STATE_H__

#include "src/buffer_variables.h"
#include "src/infrastructure/dirname.h"
#include "src/language/text/line_column.h"
#include "src/language/text/line_sequence.h"
#include "src/variables.h"

namespace afc::editor {
struct BufferVariablesInstance {
  EdgeStructInstance<bool> bool_variables =
      buffer_variables::BoolStruct()->NewInstance();
  EdgeStructInstance<std::wstring> string_variables =
      buffer_variables::StringStruct()->NewInstance();
  EdgeStructInstance<int> int_variables =
      buffer_variables::IntStruct()->NewInstance();
  EdgeStructInstance<double> double_variables =
      buffer_variables::DoubleStruct()->NewInstance();
  EdgeStructInstance<language::text::LineColumn> line_column_variables =
      buffer_variables::LineColumnStruct()->NewInstance();
};

language::text::LineSequence SerializeState(
    infrastructure::Path, language::text::LineColumn input,
    const BufferVariablesInstance& buffer_variables_instance);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_BUFFER_STATE_H__