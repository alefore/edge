#ifndef __AFC_EDITOR_TRANSFORMATION_DELETE_H__
#define __AFC_EDITOR_TRANSFORMATION_DELETE_H__

#include <memory>
#include <optional>

#include "src/line_modifier.h"
#include "src/modifiers.h"
#include "src/transformation/input.h"
#include "src/transformation/result.h"
#include "src/vm/public/environment.h"

namespace afc::editor::transformation {

struct Delete {
  std::wstring Serialize() const;

  Modifiers modifiers = Modifiers();

  enum class LineEndBehavior { kStop, kDelete };
  LineEndBehavior line_end_behavior = LineEndBehavior::kDelete;

  // When mode is kPreview, what colors should the deleted text be previewed in?
  LineModifierSet preview_modifiers = {LineModifier::RED,
                                       LineModifier::UNDERLINE};

  // If set, overrides the mode passed when the transformation is executed. This
  // is used by CompositeTransformations that want to effectively erase text
  // even in kPreview mode.
  std::optional<Input::Mode> mode = std::nullopt;

  std::optional<Range> range = std::nullopt;

  enum class Initiator {
    // The delete transformation was directly initiated by the user, requesting
    // the deletion of some contents.
    kUser,
    // The delete transformation was initiated by some other transformation, in
    // a way that doesn't fully represent that the user is deleting contents.
    kInternal
  };
  Initiator initiator;
};

std::ostream& operator<<(std::ostream& os, const Delete& options);

void RegisterDelete(language::gc::Pool& pool, vm::Environment* environment);

futures::Value<Result> ApplyBase(const Delete& parameters, Input input);
std::wstring ToStringBase(const Delete& v);
Delete OptimizeBase(Delete transformation);
}  // namespace afc::editor::transformation
#endif  // __AFC_EDITOR_TRANSFORMATION_DELETE_H__
