#include "src/delegating_output_receiver.h"

#include <glog/logging.h>

#include "src/output_receiver.h"

namespace afc {
namespace editor {

DelegatingOutputReceiver::DelegatingOutputReceiver(
    std::unique_ptr<OutputReceiver> delegate)
    : delegate_owned_(std::move(delegate)), delegate_(delegate_owned_.get()) {
  DCHECK(delegate_ != nullptr);
}

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

void DelegatingOutputReceiver::SetTabsStart(ColumnNumber columns) {
  return delegate_->SetTabsStart(columns);
}

ColumnNumber DelegatingOutputReceiver::column() { return delegate_->column(); }

ColumnNumberDelta DelegatingOutputReceiver::width() {
  return delegate_->width();
}

}  // namespace editor
}  // namespace afc
