#ifndef __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
#define __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__

#include <memory>
#include <vector>

#include "src/buffer.h"
#include "src/output_producer.h"

namespace afc::editor {

class FrameOutputProducer : public OutputProducer {
 public:
  struct Options {
    ColumnNumberDelta width = ColumnNumberDelta();
    std::wstring title;
    std::optional<size_t> position_in_parent = std::nullopt;
    enum class ActiveState { kActive, kInactive };
    ActiveState active_state = ActiveState::kInactive;
    std::wstring extra_information = L"";
    std::wstring prefix = L"";
  };

  FrameOutputProducer(Options options);

  Output Produce(LineNumberDelta lines) override;

 private:
  const std::shared_ptr<Line> line_;
};

}  // namespace afc::editor

#endif  // __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
