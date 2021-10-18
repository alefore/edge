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
#include "src/transformation/noop.h"
#include "src/transformation/stack.h"

namespace afc::editor::operation {
using futures::Past;
namespace {

Command GetDefaultCommand(TopCommandReach) { return CommandReach(); }

std::wstring SerializeCall(std::wstring name,
                           std::vector<std::wstring> arguments) {
  std::wstring output = name + L"(";
  std::wstring separator = L"";
  for (auto& a : arguments) {
    if (!a.empty()) {
      output += separator + a;
      separator = L", ";
    }
  }
  return output + L")";
}

std::wstring StructureToString(Structure* structure) {
  return structure == nullptr ? L"?" : structure->ToString();
}

Modifiers GetModifiers(Structure* structure, int repetitions,
                       Direction direction) {
  return Modifiers{
      .structure = structure == nullptr ? StructureChar() : structure,
      .direction = repetitions < 0 ? ReverseDirection(direction) : direction,
      .repetitions = abs(repetitions)};
}

Modifiers GetModifiers(Structure* structure,
                       const CommandArgumentRepetitions& repetitions,
                       Direction direction) {
  return GetModifiers(structure, repetitions.get(), direction);
}

std::wstring ToStatus(const CommandReach& reach) {
  return SerializeCall(L"Reach", {StructureToString(reach.structure),
                                  reach.repetitions.ToString()});
}

std::wstring ToStatus(const CommandReachBegin& reach) {
  return SerializeCall(
      reach.direction == Direction::kForwards ? L"Home" : L"End",
      {StructureToString(reach.structure), reach.repetitions.ToString()});
}

std::wstring ToStatus(const CommandReachLine& reach_line) {
  return SerializeCall(reach_line.repetitions.get() >= 0 ? L"Down" : L"Up",
                       {reach_line.repetitions.ToString()});
}

std::wstring ToStatus(const CommandReachChar& c) {
  return SerializeCall(L"Char",
                       {c.c.has_value() ? std::wstring(1, c.c.value()) : L"…",
                        c.repetitions.ToString()});
}

futures::Value<UndoCallback> ExecuteTransformation(
    EditorState* editor, ApplicationType application_type,
    transformation::Variant transformation) {
  // TODO(easy): Rename 'buffers_modified' to something else. Not all the
  // transformations here modify the buffers (per
  // transformation::Results::modified_buffer).
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

transformation::Stack GetTransformation(TopCommand, CommandReach reach) {
  transformation::Stack transformation;
  for (int repetitions : reach.repetitions.get_list()) {
    transformation.PushBack(transformation::ModifiersAndComposite{
        .modifiers =
            GetModifiers(reach.structure, repetitions, Direction::kForwards),
        .transformation = NewMoveTransformation()});
  }
  return transformation;
}

transformation::ModifiersAndComposite GetTransformation(
    TopCommand, CommandReachBegin reach_begin) {
  return transformation::ModifiersAndComposite{
      .modifiers = GetModifiers(reach_begin.structure, reach_begin.repetitions,
                                reach_begin.direction),
      .transformation = std::make_unique<GotoTransformation>(0)};
}

transformation::Stack GetTransformation(TopCommand,
                                        CommandReachLine reach_line) {
  transformation::Stack transformation;
  for (int repetitions : reach_line.repetitions.get_list()) {
    transformation.PushBack(transformation::ModifiersAndComposite{
        .modifiers =
            GetModifiers(StructureLine(), repetitions, Direction::kForwards),
        .transformation = NewMoveTransformation()});
  }
  return transformation;
}

transformation::Stack GetTransformation(TopCommand,
                                        CommandReachChar reach_char) {
  transformation::Stack transformation;
  if (!reach_char.c.has_value()) return transformation;
  for (int repetitions : reach_char.repetitions.get_list()) {
    transformation.PushBack(transformation::ModifiersAndComposite{
        .modifiers =
            GetModifiers(StructureChar(), repetitions, Direction::kForwards),
        .transformation =
            std::make_unique<FindTransformation>(reach_char.c.value())});
  }
  return transformation;
}

static transformation::Stack::PostTransformationBehavior
GetPostTransformationBehavior(TopCommandReach top_command) {
  return top_command.post_transformation_behavior;
}

class State {
 public:
  State(EditorState* editor_state, TopCommand top_command)
      : editor_state_(editor_state), top_command_(top_command) {}

  Command& GetLastCommand() { return commands_.back(); }

  bool empty() const { return commands_.empty(); }

  const TopCommand& top_command() const { return top_command_; }

  void set_top_command(TopCommand new_value) {
    top_command_ = new_value;
    Update();
  }

  void Push(Command command) {
    commands_.push_back(command);
    Update(ApplicationType::kPreview);
  }

  std::wstring GetStatusString() const {
    std::wstring output;
    for (const auto& op : commands_) {
      output += L" " + std::visit([](auto& t) { return ToStatus(t); }, op);
    }
    return output;
  }

  void Abort() {
    RunUndoCallback();
    editor_state_->set_keyboard_redirect(nullptr);
  }

  void Update() { Update(ApplicationType::kPreview); }

  void Commit() {
    // We make a copy because Update may delete us.
    auto editor_state = editor_state_;
    Update(ApplicationType::kCommit);
    editor_state->set_keyboard_redirect(nullptr);
  }

  void RunUndoCallback() {
    serializer_.Push(
        [callback = std::move(undo_callback_)]() { return (*callback)(); });
    undo_callback_ = std::make_shared<UndoCallback>(
        []() -> futures::Value<EmptyValue> { return Past(EmptyValue()); });
  }

  void UndoLast() {
    commands_.pop_back();
    RunUndoCallback();
    Update();
  }

 private:
  void Update(ApplicationType application_type) {
    CHECK(!commands_.empty());
    RunUndoCallback();
    transformation::Stack stack;
    for (auto& command : commands_) {
      stack.PushBack(std::visit(
          [&](auto t) -> transformation::Variant {
            return GetTransformation(top_command_, t);
          },
          command));
    }
    stack.post_transformation_behavior = std::visit(
        [](auto t) { return GetPostTransformationBehavior(t); }, top_command_);
    auto undo_callback = undo_callback_;
    StartTransformationExecution(application_type, std::move(stack))
        .SetConsumer([output = undo_callback](UndoCallback undo_callback) {
          *output = [previous = std::move(*output), undo_callback]() {
            return undo_callback().Transform(
                [previous](EmptyValue) { return previous(); });
          };
        });
  }

  // Schedules execution of a transformation through serializer_. Returns a
  // future that can be used to receive the callback that undoes the
  // transformation. The future will be notified directly in the serializer_'s
  // thread.
  futures::Value<UndoCallback> StartTransformationExecution(
      ApplicationType application_type,
      transformation::Variant transformation) {
    futures::Future<UndoCallback> output;
    serializer_.Push([editor_state = editor_state_, application_type,
                      consumer = output.consumer, transformation] {
      return ExecuteTransformation(editor_state, application_type,
                                   transformation)
          .Transform([consumer](UndoCallback undo_callback) {
            consumer(std::move(undo_callback));
            return Past(EmptyValue());
          });
    });
    return output.value;
  }

  EditorState* const editor_state_;
  futures::Serializer serializer_;
  TopCommand top_command_;
  std::vector<Command> commands_ = {};
  std::shared_ptr<UndoCallback> undo_callback_ = std::make_shared<UndoCallback>(
      []() -> futures::Value<EmptyValue> { return Past(EmptyValue()); });
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
      selected_structure = StructureSymbol();
      break;
    case L'v':
      selected_structure = StructureLine();
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
    case L'C':
      selected_structure = StructureCursor();
      break;
    case L'V':
      selected_structure = StructureTree();
      break;
    default:
      return false;
  }
  CHECK(selected_structure != nullptr);
  if (*structure == nullptr) {
    *structure = selected_structure;
    if (repetitions->get() == 0) {
      repetitions->sum(1);
    }
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
    case Terminal::BACKSPACE:
      return output->PopValue();
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
  if (output->structure == StructureChar() || output->structure == nullptr) {
    switch (c) {
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
    state->Push(GetDefaultCommand(TopCommandReach()));
    return true;
  }
  return CheckIncrementsChar(c, &output->repetitions) ||
         CheckRepetitionsChar(c, &output->repetitions);
}

class TopLevelCommandMode : public EditorMode {
 public:
  TopLevelCommandMode(TopCommand top_command, EditorState* editor_state)
      : state_(editor_state, top_command) {}

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    editor_state->status()->Reset();
    if (!state_.empty() &&
        std::visit([&](auto& t) { return ReceiveInput(&t, c, &state_); },
                   state_.GetLastCommand())) {
      state_.Update();
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
        if (!state_.empty()) {
          state_.UndoLast();
        }
        ShowStatus(editor_state);
        return;
    }

    PushDefault();
    auto old_state = state_.top_command();
    if (std::visit([&](auto& t) { return ReceiveInput(&t, c, &state_); },
                   state_.GetLastCommand())) {
      state_.Update();
      ShowStatus(editor_state);
      return;
    }
    if (std::visit([&](auto& t) { return ReceiveInputTopCommand(t, c); },
                   state_.top_command())) {
      ShowStatus(editor_state);
      return;
    }
    // Unhandled character.
    state_.UndoLast();  // The one we just pushed a few lines above.
    state_.Commit();
    editor_state->ProcessInput(c);
  }

  void ShowStatus(EditorState* editor_state) {
    editor_state->status()->SetInformationText(
        std::visit([](auto& t) { return ToStatus(t); }, state_.top_command()) +
        L":" + state_.GetStatusString());
  }

  void PushDefault() {
    PushCommand(std::visit([](auto& t) { return GetDefaultCommand(t); },
                           state_.top_command()));
  }

  void PushCommand(Command command) { state_.Push(std::move(command)); }

 private:
  bool ReceiveInputTopCommand(TopCommandReach top_command, wint_t t) {
    using PTB = transformation::Stack::PostTransformationBehavior;
    switch (t) {
      case L'd':
        switch (top_command.post_transformation_behavior) {
          case PTB::kDeleteRegion:
            top_command.post_transformation_behavior = PTB::kCopyRegion;
            break;
          case PTB::kCopyRegion:
            top_command.post_transformation_behavior = PTB::kNone;
            break;
          default:
            top_command.post_transformation_behavior = PTB::kDeleteRegion;
            break;
        }
        state_.set_top_command(top_command);
        return true;
      case L'$':
        top_command.post_transformation_behavior =
            top_command.post_transformation_behavior == PTB::kCommandSystem
                ? PTB::kNone
                : PTB::kCommandSystem;
        state_.set_top_command(top_command);
        return true;

      case L'f':
        state_.Push(CommandReachChar{});
        return true;
      case L'F':
        state_.Push(CommandReachChar{.repetitions = {.repetitions = -1}});
        return true;
      case L'j':
      case L'k':
        state_.Push(CommandReachLine{.repetitions = CommandArgumentRepetitions{
                                         .repetitions = t == L'k' ? -1 : 1}});
        return true;
      case L'H':
        state_.Push(CommandReachBegin{});
        return true;
      case L'L':
        state_.Push(CommandReachBegin{.direction = Direction::kBackwards});
        return true;
      case L'K':
        state_.Push(CommandReachBegin{.structure = StructureLine()});
        return true;
      case L'J':
        state_.Push(CommandReachBegin{.structure = StructureLine(),
                                      .direction = Direction::kBackwards});
        return true;
    }
    return false;
  }

  static std::wstring ToStatus(TopCommandReach top_command) {
    switch (top_command.post_transformation_behavior) {
      case transformation::Stack::PostTransformationBehavior::kNone:
        return L"🦋 Move";
      case transformation::Stack::PostTransformationBehavior::kDeleteRegion:
        return L"✂️  Delete";
      case transformation::Stack::PostTransformationBehavior::kCopyRegion:
        return L"📋 Copy";
      case transformation::Stack::PostTransformationBehavior::kCommandSystem:
        return L"🐚 System";
    }
    LOG(FATAL) << "Invalid post transformation behavior.";
    return L"Move";
  }

  State state_;
};
}  // namespace

std::wstring CommandArgumentRepetitions::ToString() const {
  std::wstring output;
  for (auto& r : get_list()) {
    if (!output.empty() && r > 0) {
      output += L"+";
    }
    output += std::to_wstring(r);
  }
  return output;
}

int CommandArgumentRepetitions::get() const {
  int output = 0;
  for (auto& c : get_list()) {
    output += c;
  }
  return output;
}

std::list<int> CommandArgumentRepetitions::get_list() const {
  std::list<int> output;
  for (auto& c : entries_) {
    if (Flatten(c) != 0) {
      output.push_back(Flatten(c));
    }
  }
  return output;
}

void CommandArgumentRepetitions::sum(int value) {
  if (entries_.empty() || (Flatten(entries_.back()) != 0 &&
                           Flatten(entries_.back()) >= 0) != (value >= 0)) {
    if (!entries_.empty()) {
      auto& entry_to_freeze = entries_.back();
      entry_to_freeze.additive +=
          entry_to_freeze.additive_default + entry_to_freeze.multiplicative;
      entry_to_freeze.additive_default = 0;
      entry_to_freeze.multiplicative = 0;
    }
    entries_.push_back({});  // Change of sign.
  }
  auto& last_entry = entries_.back();
  last_entry.additive +=
      value + last_entry.additive_default + last_entry.multiplicative;
  last_entry.additive_default = 0;
  last_entry.multiplicative = 0;
  last_entry.multiplicative_sign = value >= 0 ? 1 : -1;
}

void CommandArgumentRepetitions::factor(int value) {
  if (entries_.empty() || entries_.back().multiplicative == 0) {
    entries_.push_back(
        {.multiplicative_sign =
             entries_.empty() || Flatten(entries_.back()) >= 0 ? 1 : -1});
  }
  auto& last_entry = entries_.back();
  last_entry.additive_default = 0;
  last_entry.multiplicative =
      last_entry.multiplicative * 10 + last_entry.multiplicative_sign * value;
}

bool CommandArgumentRepetitions::empty() const { return entries_.empty(); }

bool CommandArgumentRepetitions::PopValue() {
  if (entries_.empty()) return false;
  entries_.pop_back();
  return true;
}

/* static */ int CommandArgumentRepetitions::Flatten(const Entry& entry) {
  return entry.additive_default + entry.additive + entry.multiplicative;
}

std::unique_ptr<afc::editor::Command> NewTopLevelCommand(
    std::wstring name, std::wstring description, TopCommand top_command,
    EditorState* editor_state, std::vector<Command> commands) {
  return NewSetModeCommand({.description = description,
                            .category = L"Edit",
                            .factory = [top_command, editor_state, commands] {
                              auto output =
                                  std::make_unique<TopLevelCommandMode>(
                                      top_command, editor_state);
                              if (commands.empty()) {
                                output->PushDefault();
                              } else {
                                for (auto& c : commands) {
                                  output->PushCommand(c);
                                }
                              }
                              output->ShowStatus(editor_state);
                              return output;
                            }});
}

}  // namespace afc::editor::operation
