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
    wstring title;
    std::optional<size_t> position_in_parent;
    enum class ActiveState { kActive, kInactive };
    ActiveState active_state = ActiveState::kInactive;
  };

  FrameOutputProducer(FrameOptions options) : options_(std::move(options)) {}

  void WriteLine(Options options) override;

 private:
  const FrameOptions options_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_FRAME_OUTPUT_PRODUCER_H__
