#pragma once

#include <memory>

#include "src/language/safe_types.h"
#include "src/operation.h"

namespace afc::editor::operation::commands {
const Description& HomeLeftDescription();
const Description& HomeRightDescription();
const Description& HomeUpDescription();
const Description& HomeDownDescription();

struct BoundaryOptions {
  Structure structure = Structure::Char;
  int repetitions = 1;
  Direction direction = Direction::Forwards;
};
language::NonNull<std::shared_ptr<MoveOperationCommand>> Boundary(
    BoundaryOptions);
}  // namespace afc::editor::operation::commands
