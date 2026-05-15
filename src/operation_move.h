#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
language::NonNull<std::shared_ptr<MoveOperationCommand>> Move(
    Structure structure, int repetitions);
}  // namespace afc::editor::operation::commands
