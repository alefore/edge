#ifndef __AFC_EDITOR_TRANSFORMATION_EXPAND_H__
#define __AFC_EDITOR_TRANSFORMATION_EXPAND_H__

#include <memory>

#include "src/language/safe_types.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"

namespace afc::editor {
language::NonNull<std::unique_ptr<CompositeTransformation>>
NewExpandTransformation();
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_EXPAND_H__
