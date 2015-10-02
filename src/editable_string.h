#ifndef __AFC_EDITOR_EDITABLE_STRING__
#define __AFC_EDITOR_EDITABLE_STRING__

#include <memory>

#include "lazy_string.h"

namespace afc {
namespace editor {

using std::shared_ptr;

class EditableString : public LazyString {
 public:
  static shared_ptr<EditableString> New(const wstring& editable_part);

  static shared_ptr<EditableString> New(
      const shared_ptr<LazyString>& base, size_t position);

  static shared_ptr<EditableString> New(
    const shared_ptr<LazyString>& base, size_t position,
    const wstring& editable_part);

  virtual wchar_t get(size_t pos) const;

  virtual size_t size() const;

  void Insert(int c);

  void Clear();

  bool Backspace();

 private:
  EditableString(const shared_ptr<LazyString>& base, size_t position,
                 const wstring& editable_part);

  shared_ptr<LazyString> base_;
  size_t position_;
  wstring editable_part_;
};

}  // namespace editor
}  // namespace afc

#endif
