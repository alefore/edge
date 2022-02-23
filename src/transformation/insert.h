#ifndef __AFC_EDITOR_TRANSFORMATION_INSERT_H__
#define __AFC_EDITOR_TRANSFORMATION_INSERT_H__

#include <memory>
#include <optional>
#include <string>

#include "src/line_modifier.h"
#include "src/modifiers.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class EditorState;
class OpenBuffer;
namespace transformation {
struct Insert {
  std::wstring Serialize() const;

  std::shared_ptr<const OpenBuffer> buffer_to_insert;

  editor::Modifiers modifiers = Modifiers();

  enum class FinalPosition {
    // Leaves the buffer position at the start of the inserted text.
    kStart,

    // Leaves the buffer position at the end of the inserted text.
    kEnd,
  };
  // Ignored if `position` is set.
  FinalPosition final_position = FinalPosition::kEnd;

  std::optional<LineModifierSet> modifiers_set = std::nullopt;

  // If not present, will insert wherever the cursor is. If present, inserts the
  // text at this position.
  std::optional<LineColumn> position = std::nullopt;
};

void RegisterInsert(editor::EditorState* editor, vm::Environment* environment);
futures::Value<Result> ApplyBase(const Insert& parameters, Input input);
std::wstring ToStringBase(const Insert& v);
Insert OptimizeBase(Insert transformation);
}  // namespace transformation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_TRANSFORMATION_INSERT_H__
