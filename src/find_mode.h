#ifndef __AFC_EDITOR_FIND_MODE_H__
#define __AFC_EDITOR_FIND_MODE_H__

#include <memory>

#include "src/transformation/composite.h"

namespace afc::editor {
class FindTransformation : public CompositeTransformation {
 public:
  FindTransformation(wchar_t c);
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;

 private:
  std::optional<language::lazy_string::ColumnNumber> SeekOnce(
      const language::text::Line& line,
      language::lazy_string::ColumnNumber column,
      const Modifiers& modifiers) const;

  const wchar_t c_;
};
}  // namespace afc::editor

#endif
