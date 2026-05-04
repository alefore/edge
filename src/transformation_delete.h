#ifndef __AFC_EDITOR_TRANSFORMATION_DELETE_H__
#define __AFC_EDITOR_TRANSFORMATION_DELETE_H__

#include <memory>
#include <optional>

#include "src/infrastructure/screen/line_modifier.h"
#include "src/language/text/line_column.h"
#include "src/modifiers.h"
#include "src/transformation_input.h"
#include "src/transformation_result.h"
#include "src/vm/environment.h"

namespace afc::editor::transformation {

struct Delete {
  std::wstring Serialize() const;

  Modifiers modifiers = Modifiers();

  enum class LineEndBehavior { Stop, Delete };
  LineEndBehavior line_end_behavior = LineEndBehavior::Delete;

  // When mode is Preview, what colors should the deleted text be previewed in?
  infrastructure::screen::LineModifierSet preview_modifiers = {
      infrastructure::screen::LineModifier::Red,
      infrastructure::screen::LineModifier::Underline};

  // If set, overrides the mode passed when the transformation is executed. This
  // is used by CompositeTransformations that want to effectively erase text
  // even in Preview mode.
  std::optional<Input::Mode> mode = std::nullopt;

  std::optional<language::text::Range> range = std::nullopt;

  enum class Initiator {
    // The delete transformation was directly initiated by the user, requesting
    // the deletion of some contents.
    User,
    // The delete transformation was initiated by some other transformation, in
    // a way that doesn't fully represent that the user is deleting contents.
    Internal
  };
  Initiator initiator;
};

std::ostream& operator<<(std::ostream& os, const Delete& options);

void RegisterDelete(language::gc::Pool& pool, vm::Environment& environment);

futures::Value<Result> ApplyBase(const Delete& parameters, Input input);
std::wstring ToStringBase(const Delete& v);
Delete OptimizeBase(Delete transformation);
}  // namespace afc::editor::transformation
#endif  // __AFC_EDITOR_TRANSFORMATION_DELETE_H__
