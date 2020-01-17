#include "src/navigate_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"

namespace afc {
namespace editor {

using std::unique_ptr;

namespace {
struct SearchRange {
  size_t begin;
  size_t end;
};

struct NavigateOptions {
  // Returns the initial range containing a given position.
  std::function<SearchRange(const OpenBuffer*, LineColumn)> initial_range;

  // Makes a new position, adjusting an existing position.
  std::function<LineColumn(LineColumn, size_t)> write_index;

  std::function<size_t(LineColumn)> position_to_index;
};

struct CursorState {
  SearchRange current;
  SearchRange initial;
};

size_t MidPoint(const CursorState& state) {
  return (state.current.begin + state.current.end) / 2;
}

struct State {
  std::unordered_map<LineColumn, CursorState> cursors;
  NavigateOptions navigate_options;
};

CursorState ComputeNextState(const NavigateOptions& navigate_options,
                             LineColumn position, CursorState state,
                             Direction direction) {
  size_t index;
  if (direction == FORWARDS) {
    state.current.begin = navigate_options.position_to_index(position);
    index = MidPoint(state);
    if (index == state.current.begin && index < state.initial.end) {
      state.current.begin++;
      state.current.end++;
    }
  } else {
    state.current.end = navigate_options.position_to_index(position);
    index = MidPoint(state);
    if (index == state.current.end && index > state.initial.begin) {
      state.current.begin--;
      state.current.end--;
    }
  }
  return state;
}

class MarkNextAnchorsTransformation : public CompositeTransformation {
 public:
  MarkNextAnchorsTransformation(std::weak_ptr<State> state) : state_(state) {}

  std::wstring Serialize() const { return L""; }

  futures::Value<Output> Apply(Input input) const {
    auto global_state = state_.lock();
    if (global_state == nullptr) return futures::Past(Output());
    auto insert_results =
        global_state->cursors.insert({input.position, CursorState{}});
    CursorState& state = insert_results.first->second;
    if (insert_results.second) {
      state.initial = global_state->navigate_options.initial_range(
          input.buffer, input.position);
      state.current = state.initial;
    }
    Output output;
    std::vector<Direction> directions = {BACKWARDS, FORWARDS};
    for (auto& direction : directions) {
      DeleteOptions delete_options;
      delete_options.copy_to_paste_buffer = false;
      delete_options.mode = Transformation::Input::Mode::kPreview;
      delete_options.modifiers.delete_type = Modifiers::PRESERVE_CONTENTS;
      output.Push(NewSetPositionTransformation(
          global_state->navigate_options.write_index(
              input.position,
              MidPoint(ComputeNextState(global_state->navigate_options,
                                        input.position, state, direction)))));
      output.Push(NewDeleteTransformation(std::move(delete_options)));
    }
    output.Push(NewSetPositionTransformation(input.position));
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<MarkNextAnchorsTransformation>(state_);
  }

 private:
  const std::weak_ptr<State> state_;
};

class NavigateTransformation : public CompositeTransformation {
 public:
  NavigateTransformation(std::weak_ptr<State> state, Direction direction)
      : state_(state), direction_(direction) {}

  std::wstring Serialize() const { return L""; }

  futures::Value<Output> Apply(Input input) const {
    auto global_state = state_.lock();
    if (global_state == nullptr) return futures::Past(Output());
    auto it = global_state->cursors.find(input.position);
    CHECK(it != global_state->cursors.end());
    CursorState state =
        ComputeNextState(global_state->navigate_options, input.position,
                         std::move(it->second), direction_);
    auto position = global_state->navigate_options.write_index(input.position,
                                                               MidPoint(state));
    global_state->cursors[position] = std::move(state);
    return futures::Past(Output::SetPosition(position));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<NavigateTransformation>(state_, direction_);
  }

 private:
  const std::weak_ptr<State> state_;
  const Direction direction_;
};

class NavigateMode : public EditorMode {
 public:
  NavigateMode(OpenBuffer* buffer, NavigateOptions options, Modifiers modifiers)
      : modifiers_(std::move(modifiers)),
        state_(std::make_shared<State>(
            State{.cursors = {}, .navigate_options = std::move(options)})) {
    buffer->ApplyToCursors(NewTransformation(
        Modifiers(), std::make_unique<MarkNextAnchorsTransformation>(state_)));
  }

  virtual ~NavigateMode() = default;

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      LOG(INFO) << "NavigateMode gives up: No current buffer.";
      return;
    }

    buffer
        ->Undo(OpenBuffer::UndoMode::kOnlyOne)  // Remove the anchors.
        .SetConsumer([this, c, buffer, editor_state](bool) {
          switch (c) {
            case 'l':
            case 'h':
              futures::Transform(
                  buffer->ApplyToCursors(NewTransformation(
                      Modifiers(),
                      std::make_unique<NavigateTransformation>(
                          state_,
                          c == 'l' ? modifiers_.direction
                                   : ReverseDirection(modifiers_.direction)))),
                  [this, buffer](bool) {
                    return buffer->ApplyToCursors(NewTransformation(
                        Modifiers(),
                        std::make_unique<MarkNextAnchorsTransformation>(
                            state_)));
                  });
              break;

            default:
              buffer->ResetMode();
              editor_state->ProcessInput(c);
          }
        });
  }

 private:
  const Modifiers modifiers_;
  const std::shared_ptr<State> state_;
};

class NavigateCommand : public Command {
 public:
  NavigateCommand() {}

  wstring Description() const override { return L"activates navigate mode."; }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    auto structure = editor_state->modifiers().structure;
    // TODO: Move to Structure.
    NavigateOptions options;
    if (structure == StructureChar()) {
      options.initial_range = [](const OpenBuffer* buffer,
                                 LineColumn position) {
        return SearchRange{0,
                           buffer->LineAt(position.line)->EndColumn().column};
      };
      options.write_index = [](LineColumn position, size_t target) {
        position.column = ColumnNumber(target);
        return position;
      };
      options.position_to_index = [](LineColumn position) {
        return position.column.column;
      };
    } else if (structure == StructureSymbol()) {
      options.initial_range = [](const OpenBuffer* buffer,
                                 LineColumn position) {
        auto contents = buffer->LineAt(position.line);
        auto contents_str = contents->ToString();
        SearchRange output;

        size_t previous_space = contents_str.find_last_not_of(
            buffer->Read(buffer_variables::symbol_characters),
            buffer->position().column.column);
        output.begin = previous_space == wstring::npos ? 0 : previous_space + 1;

        size_t next_space = contents_str.find_first_not_of(
            buffer->Read(buffer_variables::symbol_characters),
            buffer->position().column.column);
        output.end = next_space == wstring::npos ? contents->EndColumn().column
                                                 : next_space;

        return output;
      };

      options.write_index = [](LineColumn position, size_t target) {
        position.column = ColumnNumber(target);
        return position;
      };

      options.position_to_index = [](LineColumn position) {
        return position.column.column;
      };
    } else if (structure == StructureLine()) {
      options.initial_range = [](const OpenBuffer* buffer, LineColumn) {
        return SearchRange{
            0, static_cast<size_t>(buffer->contents()->size().line_delta)};
      };
      options.write_index = [](LineColumn position, size_t target) {
        position.line = LineNumber(target);
        return position;
      };
      options.position_to_index = [](LineColumn position) {
        return position.line.line;
      };
    } else {
      buffer->status()->SetInformationText(
          L"Navigate not handled for current mode.");
      buffer->ResetMode();
      return;
    }
    buffer->set_mode(std::make_unique<NavigateMode>(
        buffer.get(), std::move(options), editor_state->modifiers()));
  }

 private:
  const map<wchar_t, Command*> commands_;
  const wstring mode_description_;
};

}  // namespace

std::unique_ptr<Command> NewNavigateCommand() {
  return std::make_unique<NavigateCommand>();
}

}  // namespace editor
}  // namespace afc
