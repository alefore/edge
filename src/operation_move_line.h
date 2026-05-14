// Similar to CommandReach with structure = StructureLine.
//
// We separate them to avoid clashes of 'h' and 'l'. With CommandReach, 'h' and
// 'l' should advance by the structure; with CommandReachLine, they switch us
// back to CommandReach (to move left or right).
#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
const Description& MoveUpDescription();
const Description& MoveDownDescription();
language::NonNull<std::shared_ptr<MoveOperationCommand>> MoveLine(
    CommandArgumentRepetitions repetitions);
}  // namespace afc::editor::operation::commands
