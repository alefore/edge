#ifndef __AFC_EDITOR_CPP_COMMAND_H__
#define __AFC_EDITOR_CPP_COMMAND_H__

#include <memory>

#include "src/execution_context.h"
#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/vm.h"

namespace afc::editor {
class Command;

language::ValueOrError<language::gc::Root<Command>> NewCppCommand(
    ExecutionContext& execution_context,
    const language::lazy_string::LazyString& code);
}  // namespace afc::editor

#endif  // __AFC_EDITOR_CPP_COMMAND_H__
