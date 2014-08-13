#include "editable_string.h"

#include <cassert>
#include <memory>

#include "editor.h"
#include "lazy_string_append.h"
#include "substring.h"
#include "terminal.h"

namespace afc {
namespace editor {

shared_ptr<EditableString> EditableString::New(const string& editable_part) {
  return New(EmptyString(), 0, editable_part);
}

shared_ptr<EditableString> EditableString::New(
    const shared_ptr<LazyString>& base, size_t position) {
  return New(base, position, "");
}

shared_ptr<EditableString> EditableString::New(
    const shared_ptr<LazyString>& base, size_t position,
    const string& editable_part) {
  return shared_ptr<EditableString>(
      new EditableString(base, position, editable_part));
}

EditableString::EditableString(
    const shared_ptr<LazyString>& base, size_t position,
    const string& editable_part)
    : base_(base), position_(position), editable_part_(editable_part) {
  assert(position_ <= base_->size());
}

char EditableString::get(size_t pos) const {
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

}  // namespace afc
}  // namespace editor
