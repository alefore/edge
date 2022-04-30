#include "src/transformation/move.h"

#include <glog/logging.h>

#include <algorithm>

#include "src/buffer.h"
#include "src/buffer_variables.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/line_marks.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace afc::editor {
using language::MakeNonNullUnique;
using language::NonNull;
namespace transformation {
futures::Value<Result> ApplyBase(const SwapActiveCursor& swap_active_cursor,
                                 Input input) {
  CursorsSet& active_cursors = input.buffer.active_cursors();
  // TODO(easy, 2022-04-30): Check the iterator?
  if (input.position != *active_cursors.active()) {
    LOG(INFO) << "Skipping cursor.";
    return futures::Past(Result(input.position));
  }

  Result output(input.buffer.FindNextCursor(input.position,
                                            swap_active_cursor.modifiers));
  if (output.position == input.position) {
    LOG(INFO) << "Cursor didn't move.";
    return futures::Past(std::move(output));
  }

  VLOG(5) << "Moving cursor from " << input.position << " to "
          << output.position;

  CursorsSet::iterator next_it = active_cursors.find(output.position);
  CHECK(next_it != active_cursors.end());
  active_cursors.erase(next_it);
  active_cursors.insert(input.position);
  return futures::Past(std::move(output));
}

std::wstring ToStringBase(const SwapActiveCursor&) {
  return L"SwapActiveCursor();";
}

SwapActiveCursor OptimizeBase(SwapActiveCursor transformation) {
  return transformation;
}
};  // namespace transformation
namespace {
class MoveTransformation : public CompositeTransformation {
 public:
  MoveTransformation() {}

  std::wstring Serialize() const override { return L"MoveTransformation()"; }

  futures::Value<Output> Apply(Input input) const override {
    VLOG(1) << "Move Transformation starts: "
            << input.buffer.Read(buffer_variables::name) << " "
            << input.modifiers;
    // TODO: Finish moving to Structure.
    auto structure = input.modifiers.structure;
    if (structure == StructureCursor()) {
      return futures::Past(Output(
          transformation::SwapActiveCursor{.modifiers = input.modifiers}));
    }

    auto position = structure->Move(input.buffer, input.original_position,
                                    input.range, input.modifiers);

    if (!position.has_value()) {
      input.buffer.status().SetWarningText(L"Unhandled structure: " +
                                           structure->ToString());
      return futures::Past(Output());
    }

    if (input.modifiers.repetitions > 1) {
      input.editor.PushPosition(position.value());
    }

    LOG(INFO) << "Move from " << input.original_position << " to "
              << position.value() << " " << input.modifiers;
    return futures::Past(Output::SetPosition(position.value()));
  }
};
}  // namespace

NonNull<std::unique_ptr<CompositeTransformation>> NewMoveTransformation() {
  return MakeNonNullUnique<MoveTransformation>();
}
}  // namespace afc::editor
