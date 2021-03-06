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

  Modifiers modifiers;

  enum class LineEndBehavior { kStop, kDelete };
  LineEndBehavior line_end_behavior = LineEndBehavior::kDelete;

  // When mode is kPreview, what colors should the deleted text be previewed in?
  LineModifierSet preview_modifiers = {LineModifier::RED,
                                       LineModifier::UNDERLINE};

  // If set, overrides the mode passed when the transformation is executed. This
  // is used by CompositeTransformations that want to effectively erase text
  // even in kPreview mode.
  std::optional<Input::Mode> mode = std::nullopt;
};

std::ostream& operator<<(std::ostream& os, const Delete& options);

void RegisterDelete(vm::Environment* environment);

futures::Value<Result> ApplyBase(const Delete& parameters, Input input);
std::wstring ToStringBase(const Delete& v);
}  // namespace afc::editor::transformation
#endif  // __AFC_EDITOR_TRANSFORMATION_DELETE_H__
