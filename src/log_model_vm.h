#ifndef __AFC_EDITOR_SRC_LOG_MODEL_VM_H__
#define __AFC_EDITOR_SRC_LOG_MODEL_VM_H__

#include "src/language/error/value_or_error.h"
#include "src/vm/types.h"

namespace afc::editor {
class OpenBuffer;
const vm::Identifier& kOpenLogLineIdentifier();
language::PossibleError OpenLogLine(OpenBuffer& buffer);
}  // namespace afc::editor
#endif  // __AFC_EDITOR_SRC_LOG_MODEL_VM_H__
