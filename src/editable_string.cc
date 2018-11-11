#include "editable_string.h"

#include <cassert>
#include <memory>

#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"

namespace afc {
namespace editor {

shared_ptr<EditableString> EditableString::New(const wstring& editable_part) {
  return New(EmptyString(), 0, editable_part);
}

shared_ptr<EditableString> EditableString::New(
    const shared_ptr<LazyString>& base, size_t position) {
  return New(base, position, L"");
}

shared_ptr<EditableString> EditableString::New(
    const shared_ptr<LazyString>& base, size_t position,
    const wstring& editable_part) {
  return std::make_shared<EditableString>(
      ConstructorAccessTag(), base, position, editable_part);
}

EditableString::EditableString(
    ConstructorAccessTag, const shared_ptr<LazyString>& base, size_t position,
    const wstring& editable_part)
    : base_(base), position_(position), editable_part_(editable_part) {
  assert(position_ <= base_->size());
}

wchar_t EditableString::get(size_t pos) const {
  if (pos < position_) {
    return base_->get(pos);
  }
  if (pos - position_ < editable_part_.size()) {
    return editable_part_.at(pos - position_);
  }
  return base_->get(pos - editable_part_.size());
}

size_t EditableString::size() const {
  return base_->size() + editable_part_.size();
}

void EditableString::Insert(int c) {
  assert(c != '\n');
  editable_part_ += static_cast<char>(c);
}

bool EditableString::Backspace() {
  if (editable_part_.empty()) {
    return false;
  }
  editable_part_.resize(editable_part_.size() - 1);
  return true;
}

void EditableString::Clear() {
  editable_part_ = L"";
  base_ = EmptyString();
}

}  // namespace afc
}  // namespace editor
