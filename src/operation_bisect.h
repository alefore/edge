#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
const Description& BisectLeftDescription();
const Description& BisectRightDescription();
const Description& BisectUpDescription();
const Description& BisectDownDescription();

struct BisectOptions {
  std::optional<Structure> structure;
  std::vector<Direction> directions;
};
language::NonNull<std::shared_ptr<MoveOperationCommand>> Bisect(BisectOptions);
}  // namespace afc::editor::operation::commands
