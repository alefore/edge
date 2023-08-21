#ifndef __AFC_EDITOR_TRANSFORMATION_INSERT_H__
#define __AFC_EDITOR_TRANSFORMATION_INSERT_H__

#include <memory>
#include <optional>
#include <string>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/safe_types.h"
#include "src/modifiers.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
class EditorState;
class BufferContents;
namespace transformation {
struct Insert {
  std::wstring Serialize() const;

  // This would ideally be unique_ptr, but then `Insert` wouldn't be copyable
  // (which would make transformation::Variant non-copyable).
  language::NonNull<std::shared_ptr<const BufferContents>> contents_to_insert;

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

void RegisterInsert(language::gc::Pool& pool, vm::Environment& environment);
futures::Value<Result> ApplyBase(const Insert& parameters, Input input);
std::wstring ToStringBase(const Insert& v);
Insert OptimizeBase(Insert transformation);
}  // namespace transformation
}  // namespace afc::editor
#endif  // __AFC_EDITOR_TRANSFORMATION_INSERT_H__
