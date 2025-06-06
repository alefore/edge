#ifndef __AFC_EDITOR_FILE_TAGS_H__
#define __AFC_EDITOR_FILE_TAGS_H__

#include "src/language/gc.h"
#include "src/vm/environment.h"

namespace afc::editor {
void RegisterFileTags(language::gc::Pool& pool, vm::Environment& environment);
}

#endif  // __AFC_EDITOR_FILE_TAGS_H__
