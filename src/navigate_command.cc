#include "src/navigate_command.h"

#include <glog/logging.h>

#include <map>
#include <memory>

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/command_argument_mode.h"
#include "src/direction.h"
#include "src/editor.h"
#include "src/editor_mode.h"
#include "src/set_mode_command.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/type.h"

namespace afc::editor {
namespace {
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

// TODO(easy): Support toggling multiple_cursors.
struct NavigateOperation {
  enum class Type { kForward, kBackward, kNumber };
  Type type;
  size_t number = 0;
};

std::wstring DescribeForStatus(const NavigateOperation& operation) {
  switch (operation.type) {
    case NavigateOperation::Type::kForward:
      return L"⮞";
    case NavigateOperation::Type::kBackward:
      return L"⮜";
    case NavigateOperation::Type::kNumber:
      return std::to_wstring(operation.number + 1);
    default:
      LOG(FATAL) << "Invalid operation type.";
      return L"";
  }
}

struct NavigateState {
  NavigateOptions navigate_options;
  std::vector<NavigateOperation> operations;
};

bool CharConsumer(wint_t c, NavigateState* state) {
  switch (c) {
    case 'l':
      state->operations.push_back({NavigateOperation::Type::kForward});
      return true;

    case 'h':
      state->operations.push_back({NavigateOperation::Type::kBackward});
      return true;

    case L'1':
    case L'2':
    case L'3':
    case L'4':
    case L'5':
    case L'6':
    case L'7':
    case L'8':
    case L'9':
      state->operations.push_back(
          {NavigateOperation::Type::kNumber, .number = c - L'1'});
      return true;

    default:
      return false;
  }
}

SearchRange GetRange(const NavigateState& navigate_state,
                     const OpenBuffer* buffer, LineColumn position) {
  const SearchRange initial_range =
      navigate_state.navigate_options.initial_range(buffer, position);
  auto range = initial_range;
  size_t index = range.MidPoint();
  for (auto& operation : navigate_state.operations) {
    switch (operation.type) {
      case NavigateOperation::Type::kForward:
        if (range.size() > 1) {
          range = SearchRange(index, range.end());
          index = range.MidPoint();
        }
        if (index == range.begin() && index < initial_range.end()) {
          range = SearchRange(range.begin() + 1, range.end() + 1);
        }
        break;

      case NavigateOperation::Type::kBackward:
        if (range.size() > 1) {
          range = SearchRange(range.begin(), index);
          index = range.MidPoint();
        }
        if (index == range.begin() && index > initial_range.begin()) {
          range = SearchRange(range.begin() - 1, range.end() - 1);
        }
        break;

      case NavigateOperation::Type::kNumber: {
        double slice_width = max(1.0, range.size() / 9.0);
        double overlap = slice_width / 2;
        double new_begin = min(range.begin() + slice_width * operation.number,
                               static_cast<double>(range.end()));
        range = SearchRange(
            max(range.begin(),
                static_cast<size_t>(max(0.0, new_begin - overlap))),
            min(static_cast<size_t>(new_begin + slice_width + overlap),
                range.end()));
        break;
      }
    }
    index = range.MidPoint();
  }
  return range;
}

std::wstring BuildStatus(const NavigateState& state) {
  std::wstring output = L"navigate";
  for (const auto& operation : state.operations) {
    output = output + L" " + DescribeForStatus(operation);
  }
  return output;
}

class NavigateTransformation : public CompositeTransformation {
 public:
  NavigateTransformation(NavigateState state) : state_(std::move(state)) {}

  std::wstring Serialize() const { return L""; }

  futures::Value<Output> Apply(Input input) const {
    Output output;
    auto range = GetRange(state_, input.buffer, input.position);

    if (input.mode == transformation::Input::Mode::kPreview) {
      std::vector<NavigateOperation::Type> directions = {
          NavigateOperation::Type::kForward,
          NavigateOperation::Type::kBackward};
      for (auto& direction : directions) {
        auto state_copy = state_;
        state_copy.operations.push_back(NavigateOperation{direction});
        size_t marker_index =
            GetRange(state_copy, input.buffer, input.position).MidPoint();
        if (marker_index <= range.begin() || marker_index >= range.end())
          continue;
        LineColumn marker = state_copy.navigate_options.write_index(
            input.position, marker_index);
        if (marker != input.position) {
          output.Push(transformation::SetPosition(marker));
        }

        output.Push(transformation::Delete{
            .modifiers = {.paste_buffer_behavior =
                              Modifiers::PasteBufferBehavior::kDoNothing},
            .mode = transformation::Input::Mode::kPreview});
      }

      DeleteExterior(range.begin(), Direction::kBackwards, input.position,
                     &output);
      DeleteExterior(range.end(), Direction::kForwards, input.position,
                     &output);
    }

    output.Push(transformation::SetPosition(
        state_.navigate_options.write_index(input.position, range.MidPoint())));
    return futures::Past(std::move(output));
  }

  std::unique_ptr<CompositeTransformation> Clone() const override {
    return std::make_unique<NavigateTransformation>(state_);
  }

 private:
  // Receives one of the ends of the range (as `index`) and deletes from that
  // point on (in the direction specified).
  void DeleteExterior(size_t index, Direction direction, LineColumn position,
                      Output* output) const {
    if (index == 0 && direction == Direction::kBackwards) {
      // Otherwise we'll be saying that we want to delete the previous line.
      return;
    }
    output->Push(transformation::SetPosition(WriteIndex(position, index)));
    output->Push(transformation::Delete{
        .modifiers = {.structure = StructureLine(),
                      .direction = direction,
                      .delete_behavior = Modifiers::DeleteBehavior::kDeleteText,
                      .paste_buffer_behavior =
                          Modifiers::PasteBufferBehavior::kDoNothing},
        .line_end_behavior = transformation::Delete::LineEndBehavior::kStop,
        .preview_modifiers = {LineModifier::DIM},
        .mode = transformation::Input::Mode::kPreview});
  }

  LineColumn WriteIndex(LineColumn position, size_t index) const {
    return state_.navigate_options.write_index(position, index);
  }

  const NavigateState state_;
};

NavigateState InitialState(EditorState* editor_state) {
  auto structure = editor_state->modifiers().structure;
  // TODO: Move to Structure.
  NavigateState initial_state;
  if (structure == StructureChar()) {
    initial_state.navigate_options.initial_range = [](const OpenBuffer* buffer,
                                                      LineColumn position) {
      return SearchRange{0, buffer->LineAt(position.line)->EndColumn().column};
    };
    initial_state.navigate_options.write_index = [](LineColumn position,
                                                    size_t target) {
      position.column = ColumnNumber(target);
      return position;
    };
    initial_state.navigate_options.position_to_index = [](LineColumn position) {
      return position.column.column;
    };
  } else if (structure == StructureSymbol()) {
    initial_state.navigate_options.initial_range = [](const OpenBuffer* buffer,
                                                      LineColumn position) {
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

    initial_state.navigate_options.position_to_index = [](LineColumn position) {
      return position.column.column;
    };
  } else if (structure == StructureLine()) {
    initial_state.navigate_options.initial_range = [](const OpenBuffer* buffer,
                                                      LineColumn) {
      return SearchRange{
          0, static_cast<size_t>(buffer->contents()->size().line_delta)};
    };
    initial_state.navigate_options.write_index = [](LineColumn position,
                                                    size_t target) {
      position.line = LineNumber(target);
      return position;
    };
    initial_state.navigate_options.position_to_index = [](LineColumn position) {
      return position.line.line;
    };
  } else {
    editor_state->status()->SetInformationText(
        L"Navigate not handled for current mode.");
  }
  return initial_state;
}
}  // namespace

std::unique_ptr<Command> NewNavigateCommand(EditorState* editor_state) {
  return NewSetModeCommand(
      {.description = L"activates navigate mode.",
       .category = L"Navigate",
       .factory = [editor_state] {
         CommandArgumentMode<NavigateState>::Options options{
             .editor_state = editor_state,
             .initial_value = InitialState(editor_state),
             .char_consumer = CharConsumer,
             .status_factory = BuildStatus};
         SetOptionsForBufferTransformation<NavigateState>(
             [](EditorState*, NavigateState state) -> transformation::Variant {
               return std::make_unique<NavigateTransformation>(
                   std::move(state));
             },
             [](const NavigateState&) {
               return std::optional<Modifiers::CursorsAffected>();
             },
             &options);
         return std::make_unique<CommandArgumentMode<NavigateState>>(
             std::move(options));
       }});
}

}  // namespace afc::editor
