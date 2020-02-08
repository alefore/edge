#ifndef __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc::editor {

class FrameOutputProducer : public OutputProducer {
 public:
  // TODO(easy): Rename to `Options`.
  struct FrameOptions {
    ColumnNumberDelta width = ColumnNumberDelta();
    std::wstring title;
    std::optional<size_t> position_in_parent = std::nullopt;
    enum class ActiveState { kActive, kInactive };
    ActiveState active_state = ActiveState::kInactive;
    std::wstring extra_information = L"";
    std::wstring prefix = L"";
  };

  FrameOutputProducer(FrameOptions options);

  Generator Next() override;

 private:
  const FrameOptions options_;
  const LineModifierSet line_modifiers_;
  const LineModifierSet title_modifiers_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
