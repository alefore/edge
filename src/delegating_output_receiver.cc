#include "src/delegating_output_receiver.h"

#include <glog/logging.h>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

DelegatingOutputReceiver::DelegatingOutputReceiver(OutputReceiver* delegate)
    : delegate_(delegate) {
  DCHECK(delegate_ != nullptr);
}

void DelegatingOutputReceiver::AddCharacter(wchar_t character) {
  delegate_->AddCharacter(character);
}

void DelegatingOutputReceiver::AddString(const wstring& str) {
  delegate_->AddString(str);
}

void DelegatingOutputReceiver::AddModifier(LineModifier modifier) {
  delegate_->AddModifier(modifier);
}

void DelegatingOutputReceiver::SetTabsStart(size_t columns) {
  return delegate_->SetTabsStart(columns);
}

size_t DelegatingOutputReceiver::column() { return delegate_->column(); }

size_t DelegatingOutputReceiver::width() { return delegate_->width(); }

}  // namespace editor
}  // namespace afc
