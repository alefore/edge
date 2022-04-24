#ifndef __AFC_EDITOR_TRANSFORMATION_NOOP_H__
#define __AFC_EDITOR_TRANSFORMATION_NOOP_H__

#include <memory>

#include "src/transformation/composite.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
language::NonNull<std::unique_ptr<CompositeTransformation>>
NewNoopTransformation();
void RegisterNoopTransformation(vm::Environment* environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_NOOP_H__
