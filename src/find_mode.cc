#include "src/find_mode.h"

#include <list>
#include <memory>
#include <string>

#include "src/buffer_variables.h"
#include "src/command_mode.h"
#include "src/editor.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/set_position.h"

namespace afc {
namespace editor {

using std::shared_ptr;
using std::unique_ptr;

class FindTransformation : public CompositeTransformation {
 public:
  FindTransformation(wchar_t c) : c_(c) {}

  std::wstring Serialize() const override { return L"FindTransformation();"; }
  Output Apply(Input input) const override {
    auto line = input.buffer->LineAt(input.position.line);
    if (line == nullptr) return Output();
    ColumnNumber column = min(input.position.column, line->EndColumn());
    for (size_t i = 0; i < input.modifiers.repetitions; i++) {
      auto candidate = SeekOnce(*line, column, input.modifiers);
      if (!candidate.has_value()) break;
      column = candidate.value();
    }
    if (column == input.position.column) {
      return Output();
    }
    return Output::SetColumn(column);
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<FindTransformation>(c_);
  }

 private:
  std::optional<ColumnNumber> SeekOnce(const Line& line, ColumnNumber column,
                                       const Modifiers& modifiers) const {
    ColumnNumberDelta direction;
    ColumnNumberDelta times;
    switch (modifiers.direction) {
      case FORWARDS:
        direction = ColumnNumberDelta(1);
        times = line.EndColumn() - column;
        break;
      case BACKWARDS:
        direction = ColumnNumberDelta(-1);
        times = (column + ColumnNumberDelta(1)).ToDelta();
        break;
    }

    CHECK_GE(times, ColumnNumberDelta(0));
    for (size_t i = 1; i < static_cast<size_t>(times.column_delta); i++) {
      if (line.get(column + direction * i) == static_cast<wint_t>(c_)) {
        return column + direction * i;
      }
    }
    return std::nullopt;
  }

  const wchar_t c_;
};  // namespace editor

class FindMode : public EditorMode {
  void ProcessInput(wint_t c, EditorState* editor_state) {
    editor_state->PushCurrentPosition();
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
      buffer->ApplyToCursors(NewTransformation(
          editor_state->modifiers(), std::make_unique<FindTransformation>(c)));
      buffer->ResetMode();
    }
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
  }
};

std::unique_ptr<EditorMode> NewFindMode() {
  return std::make_unique<FindMode>();
}

}  // namespace editor
}  // namespace afc
