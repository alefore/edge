#ifndef __AFC_EDITOR_TRANSFORMATION_DELETE_H__
#define __AFC_EDITOR_TRANSFORMATION_DELETE_H__
#include <memory>

#include "src/editor.h"
#include "src/transformation.h"
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
  std::optional<Transformation::Input::Mode> mode;
};

std::ostream& operator<<(std::ostream& os, const Delete& options);

void RegisterDelete(vm::Environment* environment);

futures::Value<Transformation::Result> ApplyBase(const Delete& parameters,
                                                 Transformation::Input input);
}  // namespace afc::editor::transformation
#endif  // __AFC_EDITOR_TRANSFORMATION_DELETE_H__
