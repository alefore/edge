#ifndef __AFC_EDITOR_TRANSFORMATION_NOOP_H__
#define __AFC_EDITOR_TRANSFORMATION_NOOP_H__

#include <memory>

#include "src/transformation/composite.h"
#include "src/vm/environment.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::editor {
language::NonNull<std::unique_ptr<CompositeTransformation>>
NewNoopTransformation();
void RegisterNoopTransformation(language::gc::Pool& pool,
                                vm::Environment& environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_TRANSFORMATION_NOOP_H__
