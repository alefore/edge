#ifndef __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__

#include <optional>
#include <string>

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/text/line.h"
#include "src/language/text/line_column.h"

namespace afc::editor {

struct FrameOutputProducerOptions {
  language::lazy_string::ColumnNumberDelta width =
      language::lazy_string::ColumnNumberDelta();
  language::lazy_string::SingleLine title;
  std::optional<size_t> position_in_parent = std::nullopt;
  enum class ActiveState { kActive, kInactive };
  ActiveState active_state = ActiveState::kInactive;
  language::lazy_string::SingleLine extra_information =
      language::lazy_string::SingleLine();
  language::lazy_string::SingleLine prefix =
      language::lazy_string::SingleLine{};
};

language::text::Line FrameLine(FrameOutputProducerOptions options);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
