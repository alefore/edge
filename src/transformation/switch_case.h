#ifndef __AFC_EDITOR_TRANSFORMATION_SWITCH_CASE_H__
#define __AFC_EDITOR_TRANSFORMATION_SWITCH_CASE_H__

#include <memory>

#include "src/modifiers.h"
#include "src/transformation.h"

namespace afc::editor {
transformation::Variant NewSwitchCaseTransformation(Modifiers modifiers);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_SWITCH_CASE_H__
