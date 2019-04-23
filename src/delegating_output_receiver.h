#ifndef __AFC_EDITOR_DELEGATING_OUTPUT_RECEIVER_H__
#define __AFC_EDITOR_DELEGATING_OUTPUT_RECEIVER_H__

#include <string>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

class DelegatingOutputReceiver : public OutputReceiver {
 public:
  DelegatingOutputReceiver(std::unique_ptr<OutputReceiver> delegate);

  void AddCharacter(wchar_t character) override;
  void AddString(const wstring& str) override;
  void AddModifier(LineModifier modifier) override;
  void SetTabsStart(size_t columns) override;
  size_t column() override;
  size_t width() override;

 private:
  const std::unique_ptr<OutputReceiver> delegate_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_DELEGATING_OUTPUT_RECEIVER_H__
