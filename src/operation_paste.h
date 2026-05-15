#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
const Description& PasteDescription();
language::NonNull<std::shared_ptr<MoveOperationCommand>> Paste();
}  // namespace afc::editor::operation::commands
