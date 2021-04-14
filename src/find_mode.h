#ifndef __AFC_EDITOR_FIND_MODE_H__
#define __AFC_EDITOR_FIND_MODE_H__

#include <memory>

#include "src/command.h"
#include "src/editor.h"

namespace afc::editor {
class FindTransformation : public CompositeTransformation {
 public:
  FindTransformation(wchar_t c);
  std::wstring Serialize() const override;
  futures::Value<Output> Apply(Input input) const override;
  std::unique_ptr<CompositeTransformation> Clone() const override;

 private:
  std::optional<ColumnNumber> SeekOnce(const Line& line, ColumnNumber column,
                                       const Modifiers& modifiers) const;

  const wchar_t c_;
};
}  // namespace afc::editor

#endif
