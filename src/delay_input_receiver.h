#ifndef __AFC_EDITOR_DELAY_INPUT_RECEIVER_H__
#define __AFC_EDITOR_DELAY_INPUT_RECEIVER_H__

#include <memory>
#include <vector>

#include "src/editor_mode.h"
#include "src/infrastructure/extended_char.h"

namespace afc::editor {
class DelayInputReceiver : public SimpleInputReceiver {
  const CursorMode cursor_mode_;
  std::vector<infrastructure::ExtendedChar> input_;

 public:
  DelayInputReceiver(CursorMode cursor_mode);

  void ProcessInput(infrastructure::ExtendedChar c) override;

  CursorMode cursor_mode() const override;

  void Expand(language::gc::ObjectMetadata::Receiver&) const override {};

  const std::vector<infrastructure::ExtendedChar>& input() const;
};
}  // namespace afc::editor

#endif  // __AFC_EDITOR_DELAY_INPUT_RECEIVER_H__
