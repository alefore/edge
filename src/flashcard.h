// Ideally all the functionality here would be moved to an interpreted (by Edge
// vm) file. But there are limitations (such as the inability to create
// structures).
#ifndef __AFC_EDITOR_FLASHCARD_H__
#define __AFC_EDITOR_FLASHCARD_H__

#include "src/language/gc.h"
#include "src/vm/environment.h"

namespace afc::editor {
void RegisterFlashcard(language::gc::Pool& pool, vm::Environment& environment);
}

#endif  // __AFC_EDITOR_FLASHCARD_H__
