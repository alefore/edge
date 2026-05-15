#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
const Description& SetShellDescription();

language::NonNull<std::shared_ptr<MoveOperationCommand>> SetShell();
}  // namespace afc::editor::operation::commands
