#ifndef __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/buffer.h"
#include "src/line.h"
#include "src/line_column.h"

namespace afc::editor {

struct FrameOutputProducerOptions {
  ColumnNumberDelta width = ColumnNumberDelta();
  std::wstring title;
  std::optional<size_t> position_in_parent = std::nullopt;
  enum class ActiveState { kActive, kInactive };
  ActiveState active_state = ActiveState::kInactive;
  std::wstring extra_information = L"";
  std::wstring prefix = L"";
};

Line FrameLine(FrameOutputProducerOptions options);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
