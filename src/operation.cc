#include "src/operation.h"

#include <memory>

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/find_mode.h"
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

Command GetDefaultCommand(TopCommand top_command) {
  switch (top_command) {
    case TopCommand::kErase:
      return CommandErase();
    case TopCommand::kReach:
      return CommandReach();
  }
  LOG(FATAL) << " Invalid top command.";
  return CommandReach();
}

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

Modifiers GetModifiers(Structure* structure,
                       const CommandArgumentRepetitions& repetitions,
                       Direction direction) {
  return Modifiers{
      .structure = structure == nullptr ? StructureChar() : structure,
      .direction =
          repetitions.get() < 0 ? ReverseDirection(direction) : direction,
      .repetitions = abs(repetitions.get())};
}

std::wstring ToStatus(const CommandErase& erase) {
  return L"Erase(" + erase.repetitions.ToString() + L", " +
         (erase.structure != nullptr ? erase.structure->ToString() : L"") +
         L")";
}

std::wstring ToStatus(const CommandReach& reach) {
  return L"Reach(" + reach.repetitions.ToString() + L", " +
         (reach.structure != nullptr ? reach.structure->ToString() : L"") +
         L")";
}

std::wstring ToStatus(const CommandReachBegin& reach) {
  return std::wstring(reach.direction == Direction::kForwards ? L"Home"
                                                              : L"End") +
         L"(" +
         (reach.structure != nullptr ? reach.structure->ToString() : L"") +
         L", " + reach.repetitions.ToString() + L")";
}

std::wstring ToStatus(const CommandReachLine& reach_line) {
  return std::wstring(reach_line.repetitions.get() >= 0 ? L"Down" : L"Up") +
         L"(" + reach_line.repetitions.ToString() + L")";
}

std::wstring ToStatus(const CommandReachChar& c) {
  return L"Char(" + (c.c.has_value() ? std::wstring(1, c.c.value()) : L"…") +
         +L", " + c.repetitions.ToString() + L")";
}

bool IsNoop(const CommandErase& erase) { return erase.repetitions.get() == 0; }

bool IsNoop(const CommandReach& reach) { return reach.repetitions.get() == 0; }

bool IsNoop(const CommandReachBegin&) { return false; }

bool IsNoop(const CommandReachLine& reach_line) {
  return reach_line.repetitions.get() == 0;
}

bool IsNoop(const CommandReachChar& reach_char) {
  return !reach_char.c.has_value() || reach_char.repetitions.get() == 0;
}

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
                transformation,
                buffer->Read(buffer_variables::multiple_cursors)
                    ? Modifiers::CursorsAffected::kAll
                    : Modifiers::CursorsAffected::kOnlyCurrent,
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
  if (reach.repetitions.get() == 0)
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
              GetModifiers(reach_begin.structure, reach_begin.repetitions,
                           reach_begin.direction),
          .transformation = std::make_unique<GotoTransformation>(0)});
}

futures::Value<UndoCallback> Execute(CommandReachLine reach_line,
                                     EditorState* editor,
                                     ApplicationType application_type) {
  if (reach_line.repetitions.get() == 0)
    return Past(UndoCallback([] { return Past(EmptyValue()); }));
  return ExecuteTransformation(
      editor, application_type,
      transformation::ModifiersAndComposite{
          .modifiers = GetModifiers(StructureLine(), reach_line.repetitions,
                                    Direction::kForwards),
          .transformation = NewMoveTransformation()});
}

futures::Value<UndoCallback> Execute(CommandReachChar reach_char,
                                     EditorState* editor,
                                     ApplicationType application_type) {
  if (!reach_char.c.has_value() || reach_char.repetitions.get() == 0)
    return Past(UndoCallback([] { return Past(EmptyValue()); }));
  return ExecuteTransformation(
      editor, application_type,
      transformation::ModifiersAndComposite{
          .modifiers = GetModifiers(StructureChar(), reach_char.repetitions,
                                    Direction::kForwards),
          .transformation =
              std::make_unique<FindTransformation>(reach_char.c.value())});
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
    repetitions->sum(1);
    return true;
  }
  return true;
}

bool CheckIncrementsChar(wint_t c, CommandArgumentRepetitions* output) {
  switch (static_cast<int>(c)) {
    case L'h':
      output->sum(-1);
      return true;
    case L'l':
      output->sum(1);
      return true;
  }
  return false;
}

bool CheckRepetitionsChar(wint_t c, CommandArgumentRepetitions* output) {
  switch (static_cast<int>(c)) {
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
      output->factor(c - L'0');
      return true;
  }
  return false;
}

bool ReceiveInput(CommandErase* output, wint_t c, State* state) {
  if (CheckStructureChar(c, &output->structure, &output->repetitions) ||
      CheckIncrementsChar(c, &output->repetitions) ||
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

bool ReceiveInput(CommandReach* output, wint_t c, State*) {
  if (CheckStructureChar(c, &output->structure, &output->repetitions)) {
    return true;
  }

  if (CheckIncrementsChar(c, &output->repetitions) ||
      CheckRepetitionsChar(c, &output->repetitions)) {
    if (output->structure == nullptr) {
      output->structure = StructureChar();
    }
    return true;
  }
  return false;
}

bool ReceiveInput(CommandReachBegin* output, wint_t c, State*) {
  if (output->structure == StructureLine()) {
    switch (c) {
      case 'j':
      case 'k': {
        int delta = c == L'j' ? 1 : -1;
        if (output->direction == Direction::kBackwards) {
          delta *= -1;
        }
        output->repetitions.sum(delta);
      }
        return true;
      case 'h':
      case 'l':
        // Don't let CheckRepetitionsChar below handle these; we'd rather
        // preserve the usual meaning (of scrolling by a character).
        return false;
    }
  }
  if (CheckStructureChar(c, &output->structure, &output->repetitions) ||
      CheckIncrementsChar(c, &output->repetitions) ||
      CheckRepetitionsChar(c, &output->repetitions)) {
    return true;
  }
  return false;
}

bool ReceiveInput(CommandReachLine* output, wint_t c, State*) {
  if (CheckRepetitionsChar(c, &output->repetitions)) return true;
  switch (static_cast<int>(c)) {
    case L'j':
      output->repetitions.sum(1);
      return true;
    case L'k':
      output->repetitions.sum(-1);
      return true;
  }
  return false;
}

bool ReceiveInput(CommandReachChar* output, wint_t c, State* state) {
  if (!output->c.has_value()) {
    output->c = c;
    return true;
  }
  if (c == L' ') {
    state->Push(GetDefaultCommand(TopCommand::kReach),
                ApplicationType::kPreview);
    return true;
  }
  return CheckIncrementsChar(c, &output->repetitions) ||
         CheckRepetitionsChar(c, &output->repetitions);
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
    editor_state->status()->Reset();
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
      return;
    }
    // Unhandled character.
    state_.UndoLast();  // The one we just pushed a few lines above.
    state_.Commit();
    editor_state->ProcessInput(c);
  }

  void ShowStatus(EditorState* editor_state) {
    editor_state->status()->SetInformationText(ToStatus());
  }

 private:
  void PushDefault() {
    state_.Push(GetDefaultCommand(top_command_), ApplicationType::kPreview);
  }

  bool ReceiveInputTopCommand(wint_t t) {
    switch (top_command_) {
      case TopCommand::kErase:
        return false;
      case TopCommand::kReach:
        switch (t) {
          case L'f':
            state_.Push(CommandReachChar{}, ApplicationType::kPreview);
            return true;
          case L'j':
          case L'k':
            state_.Push(
                CommandReachLine{.repetitions =
                                     CommandArgumentRepetitions{
                                         .repetitions = t == L'k' ? -1 : 1}},
                ApplicationType::kPreview);
            return true;
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
        output += L"Erase";
        break;
      case TopCommand::kReach:
        output += L"Reach:";
        break;
    }
    return output + state_.GetStatusString();
  }

  const TopCommand top_command_;
  State state_;
};
}  // namespace

std::wstring CommandArgumentRepetitions::ToString() const {
  if (additive_default_ + additive_ == 0) {
    return std::to_wstring(get());
  }
  return std::to_wstring(additive_default_ + additive_) +
         (multiplicative_ >= 0 ? L" + " : L" - ") +
         std::to_wstring(abs(multiplicative_));
}

int CommandArgumentRepetitions::get() const {
  return additive_default_ + additive_ + multiplicative_;
}

void CommandArgumentRepetitions::sum(int value) {
  additive_ += value + additive_default_ + multiplicative_;
  additive_default_ = 0;
  multiplicative_ = 0;
  multiplicative_sign_ = value >= 0 ? 1 : -1;
}

void CommandArgumentRepetitions::factor(int value) {
  additive_default_ = 0;
  multiplicative_ = multiplicative_ * 10 + multiplicative_sign_ * value;
}

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
