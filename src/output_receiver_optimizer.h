#ifndef __AFC_EDITOR_OUTPUT_RECEIVER_OPTIMIZER_H__
#define __AFC_EDITOR_OUTPUT_RECEIVER_OPTIMIZER_H__

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/delegating_output_receiver.h"
#include "src/output_receiver.h"

namespace afc {
namespace editor {

class OutputReceiverOptimizer : public DelegatingOutputReceiver {
 public:
  OutputReceiverOptimizer(std::unique_ptr<OutputReceiver> delegate);
  ~OutputReceiverOptimizer() override;

  void AddCharacter(wchar_t character) override;
  void AddString(const wstring& str) override;
  void AddModifier(LineModifier modifier) override;
  // Returns the current column in the screen. This value may not match the
  // current column in the line, due to prefix characters (e.g., the line
  // numbers) or multi-width characters (such as \t or special unicode
  // characters).
  size_t column() override;
  size_t width() override;

 private:
  void Flush();

  LineModifierSet modifiers_;
  LineModifierSet last_modifiers_;
  wstring buffer_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_OUTPUT_RECEIVER_OPTIMIZER_H__
