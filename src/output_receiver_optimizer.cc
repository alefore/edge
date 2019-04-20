#include "src/output_receiver_optimizer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_set>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

OutputReceiverOptimizer::OutputReceiverOptimizer(
    std::unique_ptr<OutputReceiver> delegate)
    : DelegatingOutputReceiver(std::move(delegate)) {}

OutputReceiverOptimizer::~OutputReceiverOptimizer() { Flush(); }

void OutputReceiverOptimizer::AddCharacter(wchar_t character) {
  if (last_modifiers_ != modifiers_) {
    Flush();
  }
  buffer_.push_back(character);
}

void OutputReceiverOptimizer::AddString(const wstring& str) {
  if (last_modifiers_ != modifiers_) {
    Flush();
  }
  buffer_.append(str);
}

void OutputReceiverOptimizer::AddModifier(LineModifier modifier) {
  if (modifier == LineModifier::RESET) {
    modifiers_.clear();
  } else {
    modifiers_.insert(modifier);
  }
}

void OutputReceiverOptimizer::Flush() {
  DCHECK(modifiers_.find(LineModifier::RESET) == modifiers_.end());
  DCHECK(last_modifiers_.find(LineModifier::RESET) == last_modifiers_.end());

  if (!buffer_.empty()) {
    DelegatingOutputReceiver::AddString(buffer_);
    buffer_.clear();
  }

  if (!std::includes(modifiers_.begin(), modifiers_.end(),
                     last_modifiers_.begin(), last_modifiers_.end())) {
    DVLOG(5) << "last_modifiers_ is not contained in modifiers_.";
    DelegatingOutputReceiver::AddModifier(LineModifier::RESET);
    last_modifiers_.clear();
  }

  for (auto& modifier : modifiers_) {
    auto inserted = last_modifiers_.insert(modifier).second;
    if (inserted) {
      DelegatingOutputReceiver::AddModifier(modifier);
    }
  }
  DCHECK(last_modifiers_ == modifiers_);
}

size_t OutputReceiverOptimizer::column() {
  Flush();
  return DelegatingOutputReceiver::column();
}

size_t OutputReceiverOptimizer::width() {
  Flush();
  return DelegatingOutputReceiver::width();
}

}  // namespace editor
}  // namespace afc
