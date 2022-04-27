#include "src/transformation.h"

#include <glog/logging.h>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/lazy_string_append.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
namespace {
class DeleteSuffixSuperfluousCharacters : public CompositeTransformation {
 public:
  std::wstring Serialize() const override {
    return L"DeleteSuffixSuperfluousCharacters()";
  }

  futures::Value<Output> Apply(Input input) const override {
    const wstring& superfluous_characters =
        input.buffer.Read(buffer_variables::line_suffix_superfluous_characters);
    const auto line = input.buffer.LineAt(input.position.line);
    if (line == nullptr) return futures::Past(Output());
    ColumnNumber column = line->EndColumn();
    while (column > ColumnNumber(0) &&
           superfluous_characters.find(
               line->get(column - ColumnNumberDelta(1))) != string::npos) {
      --column;
    }
    if (column == line->EndColumn()) return futures::Past(Output());
    CHECK_LT(column, line->EndColumn());
    Output output = Output::SetColumn(column);

    output.Push(transformation::Delete{
        .modifiers = {.repetitions = (line->EndColumn() - column).column_delta,
                      .paste_buffer_behavior =
                          Modifiers::PasteBufferBehavior::kDoNothing}});
    return futures::Past(std::move(output));
  }
};

;
}  // namespace

transformation::Variant TransformationAtPosition(
    const LineColumn& position, transformation::Variant transformation) {
  return ComposeTransformation(transformation::SetPosition(position),
                               std::move(transformation));
}

transformation::Variant NewDeleteSuffixSuperfluousCharacters() {
  return MakeNonNullUnique<DeleteSuffixSuperfluousCharacters>();
}
}  // namespace afc::editor
