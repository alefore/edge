#ifndef __AFC_EDITOR_SHAPES_H__
#define __AFC_EDITOR_SHAPES_H__

#include "src/vm/public/environment.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::editor {
void InitShapes(language::gc::Pool& pool, vm::Environment* environment);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_SHAPES_H__
