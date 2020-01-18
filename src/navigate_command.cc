#include "src/navigate_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_with_modifiers.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"

namespace afc {
namespace editor {

using std::unique_ptr;

class SearchRange {
 public:
  SearchRange(size_t begin, size_t end) : begin_(begin), end_(end) {
    CHECK_LE(begin_, end_);
  }

  size_t begin() const { return begin_; }
  size_t end() const { return end_; }
  size_t size() const { return end_ - begin_; }

  size_t MidPoint() const { return (begin_ + end_) / 2; }

 private:
  size_t begin_;
  size_t end_;
};

struct NavigateOptions {
  // Returns the initial range containing a given position.
  std::function<SearchRange(const OpenBuffer*, LineColumn)> initial_range;

  // Makes a new position, adjusting an existing position.
  std::function<LineColumn(LineColumn, size_t)> write_index;

  std::function<size_t(LineColumn)> position_to_index;
};

struct NavigateOperation {
  enum class Type { kForward, kBackward };
  Type type;
};

struct NavigateState {
  NavigateOptions navigate_options;
  Modifiers::CursorsAffected cursors_affected;
  std::vector<NavigateOperation> operations;
};

bool TransformationArgumentApplyChar(wint_t c, NavigateState* state) {
  switch (c) {
    case 'l':
      state->operations.push_back({NavigateOperation::Type::kForward});
      return true;

    case 'h':
      state->operations.push_back({NavigateOperation::Type::kBackward});
      return true;
  }
  return false;
}

LineColumn AdjustPosition(const NavigateState& navigate_state,
                          const OpenBuffer* buffer, LineColumn position) {
  const SearchRange initial_range =
      navigate_state.navigate_options.initial_range(buffer, position);
  auto range = initial_range;
  size_t index = range.MidPoint();
  for (auto& operation : navigate_state.operations) {
    switch (operation.type) {
      case NavigateOperation::Type::kForward:
        if (range.size() > 1) range = SearchRange(index, range.end());
        index = range.MidPoint();
        if (index == range.begin() && index < initial_range.end()) {
          range = SearchRange(range.begin() + 1, range.end() + 1);
        }
        break;

      case NavigateOperation::Type::kBackward:
        if (range.size() > 1) range = SearchRange(range.begin(), index);
        index = range.MidPoint();
        if (index == range.begin() && index > initial_range.begin()) {
          range = SearchRange(range.begin() - 1, range.end() - 1);
        }
    }
  }
  return navigate_state.navigate_options.write_index(position, index);
}

std::wstring TransformationArgumentBuildStatus(const NavigateState&,
                                               std::wstring name) {
  // TODO(easy): Show information from the state?
  return name;
}

Modifiers::CursorsAffected TransformationArgumentCursorsAffected(
    const NavigateState& navigate_state) {
  return navigate_state.cursors_affected;
}

class NavigateTransformation : public CompositeTransformation {
 public:
  NavigateTransformation(NavigateState state) : state_(std::move(state)) {}

  std::wstring Serialize() const { return L""; }

  futures::Value<Output> Apply(Input input) const {
    Output output;
    if (input.mode == Transformation::Input::Mode::kPreview) {
      std::vector<NavigateOperation::Type> directions = {
          NavigateOperation::Type::kForward,
          NavigateOperation::Type::kBackward};
      for (auto& direction : directions) {
        auto state_copy = state_;
        state_copy.operations.push_back(NavigateOperation{direction});
        auto marker = AdjustPosition(state_copy, input.buffer, input.position);
        if (marker == input.position) continue;
        output.Push(NewSetPositionTransformation(marker));

        DeleteOptions delete_options;
        delete_options.copy_to_paste_buffer = false;
        delete_options.mode = Transformation::Input::Mode::kPreview;
        delete_options.modifiers.delete_type = Modifiers::PRESERVE_CONTENTS;
        output.Push(NewDeleteTransformation(std::move(delete_options)));
      }
    }
    output.Push(NewSetPositionTransformation(
        AdjustPosition(state_, input.buffer, input.position)));
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<NavigateTransformation>(state_);
  }

 private:
  const NavigateState state_;
};

class NavigateCommand : public Command {
 public:
  NavigateCommand() {}

  wstring Description() const override { return L"activates navigate mode."; }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) return;
    auto structure = editor_state->modifiers().structure;
    // TODO: Move to Structure.
    NavigateState initial_state;
    initial_state.cursors_affected =
        buffer->Read(buffer_variables::multiple_cursors)
            ? Modifiers::AFFECT_ALL_CURSORS
            : Modifiers::AFFECT_ONLY_CURRENT_CURSOR;
    if (structure == StructureChar()) {
      initial_state.navigate_options.initial_range =
          [](const OpenBuffer* buffer, LineColumn position) {
            return SearchRange{
                0, buffer->LineAt(position.line)->EndColumn().column};
          };
      initial_state.navigate_options.write_index = [](LineColumn position,
                                                      size_t target) {
        position.column = ColumnNumber(target);
        return position;
      };
      initial_state.navigate_options.position_to_index =
          [](LineColumn position) { return position.column.column; };
    } else if (structure == StructureSymbol()) {
      initial_state.navigate_options.initial_range =
          [](const OpenBuffer* buffer, LineColumn position) {
            auto contents = buffer->LineAt(position.line);
            auto contents_str = contents->ToString();
            size_t previous_space = contents_str.find_last_not_of(
                buffer->Read(buffer_variables::symbol_characters),
                buffer->position().column.column);

            size_t next_space = contents_str.find_first_not_of(
                buffer->Read(buffer_variables::symbol_characters),
                buffer->position().column.column);
            return SearchRange(
                previous_space == wstring::npos ? 0 : previous_space + 1,
                next_space == wstring::npos ? contents->EndColumn().column
                                            : next_space);
          };

      initial_state.navigate_options.write_index = [](LineColumn position,
                                                      size_t target) {
        position.column = ColumnNumber(target);
        return position;
      };

      initial_state.navigate_options.position_to_index =
          [](LineColumn position) { return position.column.column; };
    } else if (structure == StructureLine()) {
      initial_state.navigate_options.initial_range =
          [](const OpenBuffer* buffer, LineColumn) {
            return SearchRange{
                0, static_cast<size_t>(buffer->contents()->size().line_delta)};
          };
      initial_state.navigate_options.write_index = [](LineColumn position,
                                                      size_t target) {
        position.line = LineNumber(target);
        return position;
      };
      initial_state.navigate_options.position_to_index =
          [](LineColumn position) { return position.line.line; };
    } else {
      buffer->status()->SetInformationText(
          L"Navigate not handled for current mode.");
      buffer->ResetMode();
      return;
    }

    buffer->set_mode(
        std::make_unique<TransformationArgumentMode<NavigateState>>(
            L"navigate", editor_state, std::move(initial_state),
            [](EditorState*, OpenBuffer*, NavigateState state) {
              return NewTransformation(
                  Modifiers(),
                  std::make_unique<NavigateTransformation>(std::move(state)));
            }));
  }
};

std::unique_ptr<Command> NewNavigateCommand() {
  return std::make_unique<NavigateCommand>();
}

}  // namespace editor
}  // namespace afc
