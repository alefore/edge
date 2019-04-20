#ifndef __AFC_EDITOR_MERGING_DELEGATING_OUTPUT_RECEIVER_H__
#define __AFC_EDITOR_MERGING_DELEGATING_OUTPUT_RECEIVER_H__

#include <string>

#include "src/delegating_output_receiver.h"

namespace afc {
namespace editor {

// Class that merges the external modifiers with internally-produced modifiers.
class MergingDelegatingOutputReceiver : public DelegatingOutputReceiver {
 public:
  // When both internal and external modifiers are present, which set should
  // win?
  enum class Preference { kInternal, kExternal };
  MergingDelegatingOutputReceiver(std::unique_ptr<OutputReceiver> delegate,
                                  Preference preference);

  void AddModifier(LineModifier modifier) override;

 protected:
  void AddInternalModifier(LineModifier modifier);
  bool has_high_modifiers() const;

 private:
  void AddHighModifier(LineModifier modifier);
  void AddLowModifier(LineModifier modifier);

  const Preference preference_;
  bool high_modifiers_ = false;
  LineModifierSet low_modifiers_;
};

}  // namespace editor
}  // namespace afc

#endif  // __AFC_EDITOR_MERGING_DELEGATING_OUTPUT_RECEIVER_H__
