#ifndef __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc {
namespace editor {

class FrameOutputProducer : public OutputProducer {
 public:
  struct FrameOptions {
    ColumnNumberDelta width;
    wstring title;
    std::optional<size_t> position_in_parent;
    enum class ActiveState { kActive, kInactive };
    ActiveState active_state = ActiveState::kInactive;
    wstring extra_information;
  };

  FrameOutputProducer(FrameOptions options);

  Generator Next() override;

 private:
  const FrameOptions options_;
  const LineModifierSet line_modifiers_;
  const LineModifierSet title_modifiers_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
