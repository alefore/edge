#ifndef __AFC_EDITOR_TRANSFORMATION_NOOP_H__
#define __AFC_EDITOR_TRANSFORMATION_NOOP_H__

#include <memory>

#include "src/transformation.h"
#include "src/vm/public/environment.h"

namespace afc::editor {
std::unique_ptr<Transformation> NewNoopTransformation();
void RegisterNoopTransformation(vm::Environment* environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_NOOP_H__
