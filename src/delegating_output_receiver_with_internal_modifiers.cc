#include "src/delegating_output_receiver_with_internal_modifiers.h"

#include <glog/logging.h>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

DelegatingOutputReceiverWithInternalModifiers::
    DelegatingOutputReceiverWithInternalModifiers(
        std::unique_ptr<OutputReceiver> delegate, Preference preference)
    : DelegatingOutputReceiver(std::move(delegate)), preference_(preference) {}

void DelegatingOutputReceiverWithInternalModifiers::AddModifier(
    LineModifier modifier) {
  switch (preference_) {
    case Preference::kInternal:
      AddLowModifier(modifier);
      break;
    case Preference::kExternal:
      AddHighModifier(modifier);
      break;
  }
}

void DelegatingOutputReceiverWithInternalModifiers::AddInternalModifier(
    LineModifier modifier) {
  switch (preference_) {
    case Preference::kInternal:
      AddHighModifier(modifier);
      break;
    case Preference::kExternal:
      AddLowModifier(modifier);
      break;
  }
}

bool DelegatingOutputReceiverWithInternalModifiers::has_high_modifiers() const {
  return high_modifiers_;
}

void DelegatingOutputReceiverWithInternalModifiers::AddHighModifier(
    LineModifier modifier) {
  if (modifier == LineModifier::RESET) {
    if (high_modifiers_) {
      high_modifiers_ = false;
      DelegatingOutputReceiver::AddModifier(LineModifier::RESET);
      for (auto& m : low_modifiers_) {
        CHECK(m != LineModifier::RESET);
        DelegatingOutputReceiver::AddModifier(m);
      }
    }
    return;
  }

  if (!high_modifiers_) {
    if (!low_modifiers_.empty()) {
      DelegatingOutputReceiver::AddModifier(LineModifier::RESET);
    }
    high_modifiers_ = true;
  }
  DelegatingOutputReceiver::AddModifier(modifier);
  return;
}

void DelegatingOutputReceiverWithInternalModifiers::AddLowModifier(
    LineModifier modifier) {
  if (modifier == LineModifier::RESET) {
    low_modifiers_.clear();
  } else {
    low_modifiers_.insert(modifier);
  }
  if (!high_modifiers_) {
    DelegatingOutputReceiver::AddModifier(modifier);
  }
}

}  // namespace editor
}  // namespace afc
