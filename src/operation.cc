#include "src/operation.h"

#include <memory>

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/futures/futures.h"
#include "src/futures/serializer.h"
#include "src/goto_command.h"
#include "src/set_mode_command.h"
#include "src/terminal.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/move.h"

namespace afc::editor::operation {
using futures::Past;
namespace {
#if 0
bool CharConsumer(wint_t c, Modifiers* modifiers) {
  switch (c) {
    case '+':
      modifiers->repetitions = modifiers->repetitions.value_or(1) + 1;
      return true;

    case '-':
      if (modifiers->repetitions.value_or(1) > 0) {
        modifiers->repetitions = modifiers->repetitions.value_or(1) - 1;
      }
      return true;

    case '*':
      switch (modifiers->cursors_affected.value_or(
          Modifiers::kDefaultCursorsAffected)) {
        case Modifiers::CursorsAffected::kOnlyCurrent:
          modifiers->cursors_affected = Modifiers::CursorsAffected::kAll;
          break;
        case Modifiers::CursorsAffected::kAll:
          modifiers->cursors_affected =
              Modifiers::CursorsAffected::kOnlyCurrent;
          break;
      }
      return true;

    case '(':
      modifiers->boundary_begin = Modifiers::CURRENT_POSITION;
      return true;

    case '[':
      modifiers->boundary_begin = Modifiers::LIMIT_CURRENT;
      return true;

    case '{':
      modifiers->boundary_begin = Modifiers::LIMIT_NEIGHBOR;
      return true;

    case ')':
      modifiers->boundary_end = Modifiers::CURRENT_POSITION;
      return true;

    case ']':
      if (modifiers->boundary_end == Modifiers::CURRENT_POSITION) {
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
      } else if (modifiers->boundary_end == Modifiers::LIMIT_CURRENT) {
        modifiers->boundary_end = Modifiers::LIMIT_NEIGHBOR;
      } else if (modifiers->boundary_end == Modifiers::LIMIT_NEIGHBOR) {
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
        if (!modifiers->repetitions.has_value()) {
          modifiers->repetitions = 1;
        }
        ++modifiers->repetitions.value();
      }
      return true;

    case 'r':
      modifiers->direction = ReverseDirection(modifiers->direction);
      return true;


    case 'P':
      modifiers->paste_buffer_behavior =
          modifiers->paste_buffer_behavior ==
                  Modifiers::PasteBufferBehavior::kDeleteInto
              ? Modifiers::PasteBufferBehavior::kDoNothing
              : Modifiers::PasteBufferBehavior::kDeleteInto;
      return true;

    case 'k':
      modifiers->delete_behavior =
          modifiers->delete_behavior == Modifiers::DeleteBehavior::kDeleteText
              ? Modifiers::DeleteBehavior::kDoNothing
              : Modifiers::DeleteBehavior::kDeleteText;
      return modifiers;

    default:
      return false;
  }
}

std::wstring BuildStatus(
    const std::function<std::wstring(const Modifiers&)>& name,
    const Modifiers& modifiers) {
  std::wstring status = name(modifiers);
  if (modifiers.structure != StructureChar()) {
    status += L" " + modifiers.structure->ToString();
  }
  if (modifiers.direction == Direction::kBackwards) {
    status += L" reverse";
  }
  if (modifiers.cursors_affected == Modifiers::CursorsAffected::kAll) {
    status += L" multiple_cursors";
  }
  if (modifiers.repetitions.has_value()) {
    status += L" " + std::to_wstring(modifiers.repetitions.value());
  }
  if (modifiers.delete_behavior == Modifiers::DeleteBehavior::kDoNothing) {
    status += L" keep";
  }
  if (modifiers.paste_buffer_behavior ==
      Modifiers::PasteBufferBehavior::kDoNothing) {
    status += L" nuke";
  }

  status += L" ";
  switch (modifiers.boundary_begin) {
    case Modifiers::LIMIT_NEIGHBOR:
      status += L"<";
      break;
    case Modifiers::LIMIT_CURRENT:
      status += L"(";
      break;
    case Modifiers::CURRENT_POSITION:
      status += L"[";
  }
  switch (modifiers.boundary_end) {
    case Modifiers::LIMIT_NEIGHBOR:
      status += L">";
      break;
    case Modifiers::LIMIT_CURRENT:
      status += L")";
      break;
    case Modifiers::CURRENT_POSITION:
      status += L"]";
  }

  return status;
}
#endif

std::wstring ToString(const CommandArgumentRepetitions& in) {
  auto output = std::to_wstring(in.repetitions);
  switch (in.additive_behavior) {
    case CommandArgumentRepetitions::OperationBehavior::kAccept:
      output += L"+";
      break;
    case CommandArgumentRepetitions::OperationBehavior::kAcceptReset:
      output += L"?";
      break;
    case CommandArgumentRepetitions::OperationBehavior::kReject:
      output += L"|";
      break;
  }
  switch (in.multiplicative_behavior) {
    case CommandArgumentRepetitions::OperationBehavior::kAccept:
      output += L"*";
      break;
    case CommandArgumentRepetitions::OperationBehavior::kAcceptReset:
      output += L"?";
      break;
    case CommandArgumentRepetitions::OperationBehavior::kReject:
      output += L"|";
      break;
  }
  return output;
}

Modifiers GetModifiers(Structure* structure,
                       const CommandArgumentRepetitions& repetitions,
                       Direction direction) {
  return Modifiers{
      .structure = structure == nullptr ? StructureChar() : structure,
      .direction =
          repetitions.repetitions < 0 ? ReverseDirection(direction) : direction,
      .repetitions = abs(repetitions.repetitions)};
}

std::wstring ToStatus(const CommandErase& erase) {
  return L"Erase(" + ToString(erase.repetitions) + L", " +
         (erase.structure != nullptr ? erase.structure->ToString() : L"") +
         L")";
}

std::wstring ToStatus(const CommandReach& reach) {
  return L"Reach(" + ToString(reach.repetitions) + L", " +
         (reach.structure != nullptr ? reach.structure->ToString() : L"") +
         L")";
}

std::wstring ToStatus(const CommandReachBegin& reach) {
  return std::wstring(reach.direction == Direction::kForwards ? L"Home"
                                                              : L"End") +
         L"(" +
         (reach.structure != nullptr ? reach.structure->ToString() : L"") +
         L")";
}

bool IsNoop(const CommandErase& erase) {
  return erase.repetitions.repetitions == 0;
}

bool IsNoop(const CommandReach& reach) {
  return reach.repetitions.repetitions == 0;
}

bool IsNoop(const CommandReachBegin&) { return false; }

futures::Value<UndoCallback> ExecuteTransformation(
    EditorState* editor, ApplicationType application_type,
    transformation::Variant transformation) {
  auto buffers_modified =
      std::make_shared<std::vector<std::shared_ptr<OpenBuffer>>>();
  return futures::Transform(
      editor->ForEachActiveBuffer(
          [transformation = std::move(transformation), buffers_modified,
           application_type](const std::shared_ptr<OpenBuffer>& buffer) {
            buffers_modified->push_back(buffer);
            return buffer->ApplyToCursors(
                transformation, Modifiers::CursorsAffected::kOnlyCurrent,
                application_type == ApplicationType::kPreview
                    ? transformation::Input::Mode::kPreview
                    : transformation::Input::Mode::kFinal);
          }),
      [buffers_modified](EmptyValue) {
        return UndoCallback([buffers_modified] {
          return futures::Transform(
              futures::ForEach(
                  buffers_modified->begin(), buffers_modified->end(),
                  [buffers_modified](std::shared_ptr<OpenBuffer> buffer) {
                    return futures::Transform(
                        buffer->Undo(OpenBuffer::UndoMode::kOnlyOne),
                        Past(futures::IterationControlCommand::kContinue));
                  }),
              Past(EmptyValue()));
        });
      });
}

futures::Value<UndoCallback> Execute(CommandErase erase, EditorState* editor,
                                     ApplicationType application_type) {
  return ExecuteTransformation(
      editor, application_type,
      transformation::Delete{.modifiers = GetModifiers(erase.structure,
                                                       erase.repetitions,
                                                       Direction::kForwards)});
}

futures::Value<UndoCallback> Execute(CommandReach reach, EditorState* editor,
                                     ApplicationType application_type) {
  if (reach.repetitions.repetitions == 0)
    return Past(UndoCallback([] { return Past(EmptyValue()); }));
  return ExecuteTransformation(
      editor, application_type,
      transformation::ModifiersAndComposite{
          .modifiers = GetModifiers(reach.structure, reach.repetitions,
                                    Direction::kForwards),
          .transformation = NewMoveTransformation()});
}

futures::Value<UndoCallback> Execute(CommandReachBegin reach_begin,
                                     EditorState* editor,
                                     ApplicationType application_type) {
  return ExecuteTransformation(
      editor, application_type,
      transformation::ModifiersAndComposite{
          .modifiers =
              GetModifiers(reach_begin.structure,
                           CommandArgumentRepetitions{.repetitions = 1},
                           reach_begin.direction),
          .transformation = std::make_unique<GotoTransformation>(0)});
}

class State {
 public:
  State(EditorState* editor_state) : editor_state_(editor_state) {}

  Command& GetLastCommand() { return executed_commands_.back()->command; }

  bool empty() const { return executed_commands_.empty(); }

  void Push(Command command, ApplicationType application_type) {
    if (!empty() && std::visit([](auto& t) { return IsNoop(t); },
                               executed_commands_.back()->command)) {
      serializer_.Push(
          [command = executed_commands_.back()] { return command->undo(); });
      executed_commands_.pop_back();
    }

    executed_commands_.push_back(
        std::make_shared<ExecutedCommand>(ExecutedCommand{.command = command}));
    serializer_.Push([executed_command = executed_commands_.back(),
                      editor_state = editor_state_, application_type] {
      return futures::Transform(
          std::visit(
              [&](auto t) {
                return Execute(t, editor_state, application_type);
              },
              executed_command->command),
          [executed_command](UndoCallback undo_callback) {
            executed_command->undo = std::move(undo_callback);
            return Past(EmptyValue());
          });
    });
  }

  std::wstring GetStatusString() const {
    std::wstring output;
    for (const auto& op : executed_commands_) {
      output +=
          L" " + std::visit([](auto& t) { return ToStatus(t); }, op->command);
    }
    return output;
  }

  void Abort() {
    while (!empty()) UndoLast();
    editor_state_->status()->SetExpiringInformationText(L"Aborted!");
    editor_state_->set_keyboard_redirect(nullptr);
  }

  void Commit() {
    auto commands = executed_commands_;
    while (!empty()) UndoLast();
    editor_state_->ForEachActiveBuffer(
        [](const std::shared_ptr<OpenBuffer>& buffer) {
          buffer->PushTransformationStack();
          return Past(EmptyValue());
        });
    for (auto& command : commands) {
      Push(std::move(command->command), ApplicationType::kCommit);
    }
    editor_state_->ForEachActiveBuffer(
        [](const std::shared_ptr<OpenBuffer>& buffer) {
          buffer->PopTransformationStack();
          return Past(EmptyValue());
        });
    editor_state_->status()->SetExpiringInformationText(L"Executed!");
    editor_state_->set_keyboard_redirect(nullptr);
  }

  void UndoLast() {
    CHECK(!executed_commands_.empty());
    serializer_.Push(
        [command = executed_commands_.back()] { return command->undo(); });
    executed_commands_.pop_back();
  }

  void ReApplyLast() {
    CHECK(!executed_commands_.empty());
    auto command = std::move(executed_commands_.back()->command);
    UndoLast();
    Push(std::move(command), ApplicationType::kPreview);
  }

 private:
  struct ExecutedCommand {
    Command command;
    UndoCallback undo = []() -> futures::Value<EmptyValue> {
      return Past(EmptyValue());
    };
  };

  EditorState* const editor_state_;
  futures::Serializer serializer_;
  std::vector<std::shared_ptr<ExecutedCommand>> executed_commands_ = {};
};

bool Increment(CommandArgumentRepetitions* output, int delta) {
  switch (output->additive_behavior) {
    case CommandArgumentRepetitions::OperationBehavior::kAccept:
      break;
    case CommandArgumentRepetitions::OperationBehavior::kAcceptReset:
      output->repetitions = 0;
      break;
    case CommandArgumentRepetitions::OperationBehavior::kReject:
      return false;
  }

  output->repetitions = output->repetitions + delta;
  output->additive_behavior =
      CommandArgumentRepetitions::OperationBehavior::kAccept;
  output->multiplicative_behavior =
      CommandArgumentRepetitions::OperationBehavior::kReject;
  return true;
}

bool CheckStructureChar(wint_t c, Structure** structure,
                        CommandArgumentRepetitions* repetitions) {
  CHECK(structure != nullptr);
  CHECK(repetitions != nullptr);
  Structure* selected_structure = nullptr;
  switch (c) {
    case L'z':
      selected_structure = StructureChar();
      break;
    case L'x':
      selected_structure = StructureWord();
      break;
    case L'c':
      selected_structure = StructureLine();
      break;
    case L'v':
      selected_structure = StructureTree();
      break;
    case L'b':
      selected_structure = StructureParagraph();
      break;
    case L'n':
      selected_structure = StructurePage();
      break;
    case L'm':
      selected_structure = StructureBuffer();
      break;
    default:
      return false;
  }
  CHECK(selected_structure != nullptr);
  if (*structure == nullptr) {
    *structure = selected_structure;
  } else if (selected_structure != *structure) {
    return false;
  } else {
    return Increment(repetitions, 1);
  }
  return true;
}

bool CheckRepetitionsChar(wint_t c, CommandArgumentRepetitions* output) {
  switch (static_cast<int>(c)) {
    case L'h':
      return Increment(output, -1);
    case L'l':
      return Increment(output, 1);
    case L'0':
    case L'1':
    case L'2':
    case L'3':
    case L'4':
    case L'5':
    case L'6':
    case L'7':
    case L'8':
    case L'9':
      switch (output->multiplicative_behavior) {
        case CommandArgumentRepetitions::OperationBehavior::kReject:
          return false;
        case CommandArgumentRepetitions::OperationBehavior::kAcceptReset:
          output->repetitions = 0;
          output->multiplicative_behavior =
              CommandArgumentRepetitions::OperationBehavior::kAccept;
          break;
        case CommandArgumentRepetitions::OperationBehavior::kAccept:
          break;  // Nothing.
      }
      output->repetitions = output->repetitions * 10 + c - L'0';
      break;
    default:
      return false;
  }
  return true;
}

bool ReceiveInput(CommandErase* output, wint_t c, State* state) {
  if (CheckStructureChar(c, &output->structure, &output->repetitions) ||
      CheckRepetitionsChar(c, &output->repetitions)) {
    return true;
  }

  switch (c) {
    case L'e':
      state->Push(CommandErase(), ApplicationType::kPreview);
      return true;
  }
  return false;
}

bool ReceiveInput(CommandReach* output, wint_t c, State* state) {
  if (CheckStructureChar(c, &output->structure, &output->repetitions)) {
    return true;
  }
  if (CheckRepetitionsChar(c, &output->repetitions)) {
    if (output->structure == nullptr) {
      output->structure = StructureChar();
    }
    return true;
  }
  switch (static_cast<int>(c)) {
    case L'h':
    case L'l': {
      int delta = c == L'h' ? -1 : 1;
      if (output->structure == StructureChar() ||
          output->structure == nullptr) {
        output->structure = StructureChar();
        output->repetitions.additive_behavior =
            CommandArgumentRepetitions::OperationBehavior::kAccept;
        output->repetitions.multiplicative_behavior =
            output->repetitions.repetitions == 0
                ? CommandArgumentRepetitions::OperationBehavior::kAcceptReset
                : CommandArgumentRepetitions::OperationBehavior::kReject;
        output->repetitions.repetitions += delta;
        return true;
      }
      return false;
    }

    case L'k':
    case L'j': {
      int delta = c == L'k' ? -1 : 1;
      if (output->structure == StructureLine() ||
          output->structure == nullptr) {
        output->structure = StructureLine();
        output->repetitions.additive_behavior =
            CommandArgumentRepetitions::OperationBehavior::kReject;
        output->repetitions.multiplicative_behavior =
            output->repetitions.repetitions == 0
                ? CommandArgumentRepetitions::OperationBehavior::kAcceptReset
                : CommandArgumentRepetitions::OperationBehavior::kReject;
        output->repetitions.repetitions += delta;
        return true;
      }
      return false;
    }
    default:
      return false;
  }
  return true;
}

bool ReceiveInput(CommandReachBegin* output, wint_t c, State*) {
  CommandArgumentRepetitions repetitions_dummy;
  if (CheckStructureChar(c, &output->structure, &repetitions_dummy)) {
    return true;
  }
  switch (static_cast<int>(c)) {
    default:
      return false;
  }
  return true;
}

#if 0
{
  // TODO: Get rid of this cast, ugh.
  switch (static_cast<int>(c)) {
    case Terminal::BACKSPACE:
      if (data_->executed_operations.empty()) return;
      if (std::visit([&](auto t) { return ConsumeBackspace(t); },
                     data_->executed_operations.back().operation)) {
        return ReApplyLast();
      }
      return futures::Transform(UndoLast(), [data](EmptyValue) {
        data->operations.pop_back();
        return Success();
      });


    default:
      if (ApplyChar(c, &argument)) {
        argument_string_.push_back(c);
        return Transform(CommandArgumentModeApplyMode::kPreview, argument);
      }
      return futures::Transform(
          static_cast<int>(c) == Terminal::ESCAPE
              ? futures::Past(EmptyValue())
              : Transform(CommandArgumentModeApplyMode::kFinal, argument),
          [editor_state, c](EmptyValue) {
            editor_state->status()->Reset();
            auto editor_state_copy = editor_state;
            editor_state->set_keyboard_redirect(nullptr);
            if (c != L'\n') {
              editor_state_copy->ProcessInput(c);
            }
            return futures::Past(EmptyValue());
          });
  }
}
#endif

class TopLevelCommandMode : public EditorMode {
 public:
  TopLevelCommandMode(TopCommand top_command, EditorState* editor_state)
      : top_command_(top_command), state_(editor_state) {
    PushDefault();
  }

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    if (!state_.empty() &&
        std::visit([&](auto& t) { return ReceiveInput(&t, c, &state_); },
                   state_.GetLastCommand())) {
      state_.ReApplyLast();
      ShowStatus(editor_state);
      return;
    }

    switch (static_cast<int>(c)) {
      case Terminal::ESCAPE:
        state_.Abort();
        return;
      case L'\n':
        state_.Commit();
        return;
      case Terminal::BACKSPACE:
        state_.UndoLast();
        ShowStatus(editor_state);
        return;
    }

    PushDefault();
    if (std::visit([&](auto& t) { return ReceiveInput(&t, c, &state_); },
                   state_.GetLastCommand()) ||
        ReceiveInputTopCommand(c)) {
      state_.ReApplyLast();
      ShowStatus(editor_state);
    } else {
      state_.UndoLast();
      state_.Commit();
      editor_state->ProcessInput(c);
    }
  }

  void ShowStatus(EditorState* editor_state) {
    editor_state->status()->SetInformationText(ToStatus());
  }

 private:
  void PushDefault() {
    state_.Push(GetDefaultCommand(), ApplicationType::kPreview);
  }

  Command GetDefaultCommand() {
    switch (top_command_) {
      case TopCommand::kErase:
        return CommandErase();
      case TopCommand::kReach:
        return CommandReach();
    }
    LOG(FATAL) << " Invalid top command.";
    return CommandReach();
  }

  bool ReceiveInputTopCommand(wint_t t) {
    switch (top_command_) {
      case TopCommand::kErase:
        return false;
      case TopCommand::kReach:
        switch (t) {
          case L'H':
            state_.Push(CommandReachBegin{}, ApplicationType::kPreview);
            return true;
          case L'L':
            state_.Push(CommandReachBegin{.direction = Direction::kBackwards},
                        ApplicationType::kPreview);
            return true;
          case L'K':
            state_.Push(CommandReachBegin{.structure = StructureLine()},
                        ApplicationType::kPreview);
            return true;
          case L'J':
            state_.Push(CommandReachBegin{.structure = StructureLine(),
                                          .direction = Direction::kBackwards},
                        ApplicationType::kPreview);
            return true;
        }
        return false;
    }
    return false;
  }

  std::wstring ToStatus() {
    std::wstring output;
    switch (top_command_) {
      case TopCommand::kErase:
        output += L"erase";
        break;
      case TopCommand::kReach:
        output += L"reach";
        break;
    }
    return output + state_.GetStatusString();
  }

  const TopCommand top_command_;
  State state_;
};
}  // namespace

std::unique_ptr<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, std::wstring description, TopCommand top_command,
    EditorState* editor_state) {
  return NewSetModeCommand({.description = description,
                            .category = L"Edit",
                            .factory = [top_command, editor_state] {
                              auto output =
                                  std::make_unique<TopLevelCommandMode>(
                                      top_command, editor_state);
                              output->ShowStatus(editor_state);
                              return output;
                            }});
}

}  // namespace afc::editor::operation
