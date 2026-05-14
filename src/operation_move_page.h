#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
const Description& MovePageUpDescription();
const Description& MovePageDownDescription();
language::NonNull<std::shared_ptr<MoveOperationCommand>> MovePage(
    Repetitions repetitions);
}  // namespace afc::editor::operation::commands
