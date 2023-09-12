#include "src/operation.h"

#include <memory>

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/find_mode.h"
#include "src/futures/futures.h"
#include "src/futures/serializer.h"
#include "src/goto_command.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/safe_types.h"
#include "src/operation_scope.h"
#include "src/set_mode_command.h"
#include "src/terminal.h"
#include "src/transformation/bisect.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/move.h"
#include "src/transformation/noop.h"
#include "src/transformation/reach_query.h"
#include "src/transformation/stack.h"

namespace afc::editor::operation {
using futures::Past;
using infrastructure::Tracker;
using infrastructure::screen::VisualOverlayMap;
using language::EmptyValue;
using language::MakeNonNullUnique;
using language::NonNull;
using language::lazy_string::Append;
using language::lazy_string::NewLazyString;

namespace gc = language::gc;

namespace {
using UndoCallback = std::function<futures::Value<EmptyValue>()>;

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

std::wstring StructureToString(std::optional<Structure> structure) {
  std::ostringstream oss;
  if (structure.has_value())
    oss << *structure;
  else
    oss << "?";
  return language::FromByteString(oss.str());
}

Modifiers GetModifiers(std::optional<Structure> structure, int repetitions,
                       Direction direction) {
  return Modifiers{
      .structure = structure.value_or(Structure::kChar),
      .direction = repetitions < 0 ? ReverseDirection(direction) : direction,
      .repetitions = abs(repetitions)};
}

Modifiers GetModifiers(std::optional<Structure> structure,
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

std::wstring ToStatus(const CommandReachPage& reach_line) {
  return SerializeCall(reach_line.repetitions.get() >= 0 ? L"PgDown" : L"PgUp",
                       {reach_line.repetitions.ToString()});
}

std::wstring ToStatus(const CommandReachQuery& c) {
  return SerializeCall(
      L"Query",
      {c.query + std::wstring(3 - std::min(3ul, c.query.size()), L'_')});
}

std::wstring ToStatus(const CommandReachBisect& c) {
  std::wstring directions;
  wchar_t backwards = c.structure == Structure::kLine ? L'↑' : L'←';
  wchar_t forwards = c.structure == Structure::kLine ? L'↓' : L'→';
  for (Direction direction : c.directions) switch (direction) {
      case Direction::kForwards:
        directions.push_back(forwards);
        break;
      case Direction::kBackwards:
        directions.push_back(backwards);
        break;
    }
  return SerializeCall(L"Bisect", {StructureToString(c.structure), directions});
}

std::wstring ToStatus(const CommandSetShell& c) {
  return SerializeCall(L"|", {c.input});
}

futures::Value<UndoCallback> ExecuteTransformation(
    EditorState& editor, ApplicationType application_type,
    transformation::Variant transformation) {
  static Tracker top_tracker(L"ExecuteTransformation");
  auto top_call = top_tracker.Call();

  auto buffers_transformed =
      std::make_shared<std::vector<gc::Root<OpenBuffer>>>();
  return editor
      .ForEachActiveBuffer([transformation = std::move(transformation),
                            buffers_transformed,
                            application_type](OpenBuffer& buffer) {
        static Tracker tracker(L"ExecuteTransformation::ApplyTransformation");
        auto call = tracker.Call();
        buffers_transformed->push_back(buffer.NewRoot());
        return buffer.ApplyToCursors(
            transformation,
            buffer.Read(buffer_variables::multiple_cursors)
                ? Modifiers::CursorsAffected::kAll
                : Modifiers::CursorsAffected::kOnlyCurrent,
            application_type == ApplicationType::kPreview
                ? transformation::Input::Mode::kPreview
                : transformation::Input::Mode::kFinal);
      })
      .Transform([buffers_transformed](EmptyValue) {
        return UndoCallback([buffers_transformed] {
          static Tracker tracker(L"ExecuteTransformation::Undo");
          auto call = tracker.Call();
          return futures::ForEach(
                     buffers_transformed->begin(), buffers_transformed->end(),
                     [buffers_transformed](gc::Root<OpenBuffer> buffer) {
                       return buffer.ptr()
                           ->Undo(UndoState::ApplyOptions::Mode::kOnlyOne,
                                  UndoState::ApplyOptions::RedoMode::kIgnore)
                           .Transform([](auto) {
                             return futures::IterationControlCommand::kContinue;
                           });
                     })
              .Transform([](auto) { return EmptyValue(); });
        });
      });
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
    transformation::Stack&, CommandReach reach) {
  transformation::Stack transformation;
  for (int repetitions : reach.repetitions.get_list()) {
    transformation.PushBack(transformation::ModifiersAndComposite{
        .modifiers =
            GetModifiers(reach.structure, repetitions, Direction::kForwards),
        .transformation = NewMoveTransformation(operation_scope)});
  }
  return transformation;
}

transformation::ModifiersAndComposite GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>&, transformation::Stack&,
    CommandReachBegin reach_begin) {
  return transformation::ModifiersAndComposite{
      .modifiers = GetModifiers(reach_begin.structure, reach_begin.repetitions,
                                reach_begin.direction),
      .transformation = MakeNonNullUnique<GotoTransformation>(0)};
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
    transformation::Stack&, CommandReachLine reach_line) {
  transformation::Stack transformation;
  for (int repetitions : reach_line.repetitions.get_list()) {
    transformation.PushBack(transformation::ModifiersAndComposite{
        .modifiers =
            GetModifiers(Structure::kLine, repetitions, Direction::kForwards),
        .transformation = NewMoveTransformation(operation_scope)});
  }
  return transformation;
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
    transformation::Stack&, CommandReachPage reach_page) {
  transformation::Stack transformation;
  for (int repetitions : reach_page.repetitions.get_list()) {
    transformation.PushBack(transformation::ModifiersAndComposite{
        .modifiers =
            GetModifiers(Structure::kPage, repetitions, Direction::kForwards),
        .transformation = NewMoveTransformation(operation_scope)});
  }
  return transformation;
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>&, transformation::Stack&,
    CommandReachQuery reach_query) {
  if (reach_query.query.empty()) return transformation::Stack{};
  transformation::Stack transformation;
  transformation.PushBack(
      MakeNonNullUnique<transformation::ReachQueryTransformation>(
          reach_query.query));
  return transformation;
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>&, transformation::Stack&,
    CommandReachBisect bisect) {
  transformation::Stack transformation;
  transformation.PushBack(MakeNonNullUnique<transformation::Bisect>(
      bisect.structure.value_or(Structure::kChar),
      std::move(bisect.directions)));
  return transformation;
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>&,
    transformation::Stack& stack, CommandSetShell shell) {
  stack.post_transformation_behavior =
      transformation::Stack::PostTransformationBehavior::kCommandSystem;
  stack.shell = transformation::ShellCommand(shell.input);
  return transformation::Stack{};
}

class State {
 public:
  State(EditorState& editor_state, TopCommand top_command)
      : editor_state_(editor_state), top_command_(std::move(top_command)) {}

  Command& GetLastCommand() { return commands_.back(); }

  bool empty() const { return commands_.empty(); }

  const TopCommand& top_command() const { return top_command_; }

  void set_top_command(TopCommand new_value) {
    top_command_ = std::move(new_value);
    Update();
  }

  void Push(Command command) {
    static Tracker tracker(L"State::Push");
    auto call = tracker.Call();
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
    editor_state_.set_keyboard_redirect(nullptr);
  }

  void Update() { Update(ApplicationType::kPreview); }

  void Commit() {
    static Tracker tracker(L"State::Commit");
    auto call = tracker.Call();
    // We make a copy because Update may delete us.
    EditorState& editor_state = editor_state_;
    Update(ApplicationType::kCommit);
    editor_state.set_keyboard_redirect(nullptr);
  }

  void RunUndoCallback() {
    static Tracker tracker(L"State::RunUndoCallback");
    auto call = tracker.Call();
    const EditorState& editor = editor_state_;
    const std::shared_ptr<EditorMode> keyboard_redirect =
        editor.keyboard_redirect();
    serializer_.Push([callback = std::move(undo_callback_)]() {
      return Pointer(callback).Reference()();
    });
    CHECK_EQ(keyboard_redirect, editor.keyboard_redirect())
        << "Internal error: undo callback has changed the keyboard redirector, "
           "probably causing us to be deleted. This isn't supported (as this "
           "code assumes survival of various now-deleted objects).";

    undo_callback_ = std::make_shared<UndoCallback>(
        []() -> futures::Value<EmptyValue> { return Past(EmptyValue()); });
  }

  void UndoLast() {
    static Tracker tracker(L"State::UndoLast");
    auto call = tracker.Call();
    commands_.pop_back();
    if (commands_.empty()) Push(CommandReach());
    RunUndoCallback();
    Update();
  }

 private:
  void Update(ApplicationType application_type) {
    static Tracker tracker(L"State::Update");
    auto call = tracker.Call();
    CHECK(!commands_.empty());
    RunUndoCallback();
    std::shared_ptr<UndoCallback> original_undo_callback = undo_callback_;
    StartTransformationExecution(application_type, PrepareStack())
        .SetConsumer([original_undo_callback](UndoCallback undo_callback) {
          *original_undo_callback =
              [previous = std::move(*original_undo_callback), undo_callback]() {
                return undo_callback().Transform(
                    [previous](EmptyValue) { return previous(); });
              };
        });
  }

  transformation::Variant PrepareStack() {
    transformation::Stack stack;
    stack.post_transformation_behavior =
        top_command_.post_transformation_behavior;
    // After each transformation (except for the last), we reset the visual
    // overlays. This allows us to clean up in case we have a
    // transformation::Bisect leaves visual overlays (that are no longer
    // relevant, since other transformations follow).
    std::optional<transformation::Variant> separator;
    for (auto& command : commands_) {
      if (separator.has_value()) stack.PushBack(separator.value());
      stack.PushBack(std::visit(
          [&](auto t) -> transformation::Variant {
            static Tracker tracker(L"State::PrepareStack::GetTransformation");
            auto call = tracker.Call();
            return GetTransformation(operation_scope_, stack, t);
          },
          command));
      separator = transformation::VisualOverlay{.visual_overlay_map =
                                                    VisualOverlayMap()};
    }
    return OptimizeBase(stack);
  }

  // Schedules execution of a transformation through serializer_. Returns a
  // future that can be used to receive the callback that undoes the
  // transformation. The future will be notified directly in the serializer_'s
  // thread.
  futures::Value<UndoCallback> StartTransformationExecution(
      ApplicationType application_type,
      transformation::Variant transformation) {
    futures::Future<UndoCallback> output;
    serializer_.Push([&editor_state = editor_state_, application_type,
                      consumer = output.consumer, transformation] {
      return ExecuteTransformation(editor_state, application_type,
                                   transformation)
          .Transform([consumer](UndoCallback undo_callback) {
            consumer(std::move(undo_callback));
            return Past(EmptyValue());
          });
    });
    return std::move(output.value);
  }

  EditorState& editor_state_;
  NonNull<std::shared_ptr<OperationScope>> operation_scope_;
  futures::Serializer serializer_;
  TopCommand top_command_;
  std::vector<Command> commands_ = {};
  std::shared_ptr<UndoCallback> undo_callback_ = std::make_shared<UndoCallback>(
      []() -> futures::Value<EmptyValue> { return Past(EmptyValue()); });
};

class KeyCommandsMap {
 public:
  struct KeyCommand {
    std::wstring description = L"";
    bool active = true;
    std::function<void(wchar_t)> handler;
  };

  KeyCommandsMap& Insert(wchar_t c, KeyCommand command) {
    if (command.active) table_.insert({c, std::move(command)});
    return *this;
  }

  KeyCommandsMap Erase(wchar_t c) {
    table_.erase(c);
    return *this;
  }

  bool Execute(wchar_t c) const {
    if (auto it = table_.find(c); it != table_.end()) {
      it->second.handler(c);
      return true;
    }
    return false;
  }

 private:
  std::unordered_map<wchar_t, KeyCommand> table_;
};

void CheckStructureChar(KeyCommandsMap& cmap,
                        std::optional<Structure>* structure,
                        CommandArgumentRepetitions* repetitions) {
  CHECK(structure != nullptr);
  CHECK(repetitions != nullptr);

  auto add_key = [&](wchar_t c, Structure selected_structure) {
    LOG(INFO) << "Add key: " << selected_structure;
    cmap.Insert(c, {.active = *structure == std::nullopt,
                    .handler =
                        [structure, repetitions, selected_structure](wchar_t) {
                          LOG(INFO)
                              << "Running, storing: " << selected_structure;
                          *structure = selected_structure;
                          if (repetitions->get() == 0) {
                            repetitions->sum(1);
                          }
                        }})
        .Insert(c,
                {.active = selected_structure == *structure,
                 .handler = [repetitions](wchar_t) { repetitions->sum(1); }});
  };

  add_key(L'z', Structure::kChar);
  add_key(L'x', Structure::kWord);
  add_key(L'c', Structure::kSymbol);
  add_key(L'v', Structure::kLine);
  add_key(L'b', Structure::kParagraph);
  add_key(L'n', Structure::kPage);
  add_key(L'm', Structure::kBuffer);
  add_key(L'C', Structure::kCursor);
  add_key(L'V', Structure::kTree);
}

void CheckIncrementsChar(KeyCommandsMap& cmap,
                         CommandArgumentRepetitions* output) {
  cmap.Insert(L'h', {.handler = [output](wchar_t) { output->sum(-1); }})
      .Insert(L'l', {.handler = [output](wchar_t) { output->sum(1); }});
}

void CheckRepetitionsChar(KeyCommandsMap& cmap,
                          CommandArgumentRepetitions* output) {
  cmap.Insert(Terminal::BACKSPACE,
              {.active = !output->empty(),
               .handler = [output](wchar_t) { output->PopValue(); }});
  for (int i = 0; i < 10; i++)
    cmap.Insert(L'0' + i,
                {.handler = [output, i](wchar_t) { output->factor(i); }});
}

bool ReceiveInput(CommandReach* output, wint_t c, State* state) {
  KeyCommandsMap cmap;
  if (output->structure.value_or(Structure::kChar) == Structure::kChar &&
      !output->repetitions.empty()) {
    cmap.Insert(L'H', {.active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](wchar_t) {
                             state->Push(CommandReachBisect{
                                 .structure = Structure::kChar,
                                 .directions = {Direction::kBackwards}});
                           }})
        .Insert(L'L', {.active = output->repetitions.get_list().back() > 0,
                       .handler = [state](wchar_t) {
                         state->Push(CommandReachBisect{
                             .structure = Structure::kChar,
                             .directions = {Direction::kForwards}});
                       }});
  }

  if (output->structure == Structure::kLine && !output->repetitions.empty()) {
    cmap.Insert(L'K', {.active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](wchar_t) {
                             state->Push(CommandReachBisect{
                                 .structure = Structure::kLine,
                                 .directions = {Direction::kBackwards}});
                           }})
        .Insert(L'J', {.active = output->repetitions.get_list().back() > 0,
                       .handler = [state](wchar_t) {
                         state->Push(CommandReachBisect{
                             .structure = Structure::kLine,
                             .directions = {Direction::kForwards}});
                       }});
  }

  CheckStructureChar(cmap, &output->structure, &output->repetitions);
  CheckIncrementsChar(cmap, &output->repetitions);
  CheckRepetitionsChar(cmap, &output->repetitions);
  if (cmap.Execute(c)) {
    if (output->structure == std::nullopt) output->structure = Structure::kChar;
    return true;
  }

  return false;
}

bool ReceiveInput(CommandReachBegin* output, wint_t c, State*) {
  KeyCommandsMap cmap;
  if (output->structure == Structure::kLine) {
    KeyCommandsMap::KeyCommand handler = {.handler = [output](wchar_t t) {
      int delta = t == L'j' ? 1 : -1;
      if (output->direction == Direction::kBackwards) {
        delta *= -1;
      }
      output->repetitions.sum(delta);
    }};
    cmap.Insert(L'j', handler).Insert(L'k', handler);
  }

  CheckStructureChar(cmap, &output->structure, &output->repetitions);
  CheckIncrementsChar(cmap, &output->repetitions);
  CheckRepetitionsChar(cmap, &output->repetitions);

  if (output->structure.value_or(Structure::kChar) == Structure::kChar ||
      output->structure == Structure::kLine) {
    // Don't let CheckRepetitionsChar below handle these; we'd rather preserve
    // the usual meaning (of scrolling by a character).
    cmap.Erase(L'h').Erase(L'l');
  }

  return cmap.Execute(c);
}

bool ReceiveInput(CommandReachLine* output, wint_t c, State* state) {
  KeyCommandsMap cmap;
  cmap.Insert(L'K', {.active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() < 0,
                     .handler =
                         [state](wchar_t) {
                           state->Push(CommandReachBisect{
                               .structure = Structure::kLine,
                               .directions = {Direction::kBackwards}});
                         }})
      .Insert(L'J', {.active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() > 0,
                     .handler = [state](wchar_t) {
                       state->Push(CommandReachBisect{
                           .structure = Structure::kLine,
                           .directions = {Direction::kForwards}});
                     }});

  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(L'j',
              {.handler = [output](wchar_t) { output->repetitions.sum(1); }})
      .Insert(L'k',
              {.handler = [output](wchar_t) { output->repetitions.sum(-1); }});
  return cmap.Execute(c);
}

bool ReceiveInput(CommandReachPage* output, wint_t c, State*) {
  KeyCommandsMap cmap;
  CheckRepetitionsChar(cmap, &output->repetitions);
  return cmap
      .Insert(Terminal::PAGE_DOWN,
              {.handler = [output](wchar_t) { output->repetitions.sum(1); }})
      .Insert(Terminal::PAGE_UP,
              {.handler = [output](wchar_t) { output->repetitions.sum(-1); }})
      .Execute(c);
}

bool ReceiveInput(CommandReachQuery* output, wint_t c, State*) {
  if (c == '\n' || c == Terminal::ESCAPE) return false;
  if (static_cast<int>(c) == Terminal::BACKSPACE) {
    if (output->query.empty()) return false;
    output->query.pop_back();
    return true;
  }
  if (output->query.size() < 3) {
    output->query.push_back(c);
    return true;
  }
  return false;
}

bool ReceiveInput(CommandReachBisect* output, wint_t c, State*) {
  KeyCommandsMap cmap;

  cmap.Insert(Terminal::BACKSPACE, {.active = !output->directions.empty(),
                                    .handler = [output](wchar_t) {
                                      return output->directions.pop_back();
                                    }});

  if (output->structure.value_or(Structure::kChar) == Structure::kChar) {
    cmap.Insert(L'h',
                {.handler =
                     [output](wchar_t) {
                       output->directions.push_back(Direction::kBackwards);
                     }})
        .Insert(L'l', {.handler = [output](wchar_t) {
                  output->directions.push_back(Direction::kForwards);
                }});
  }
  if (output->structure == Structure::kLine) {
    cmap.Insert(L'k',
                {.handler =
                     [output](wchar_t) {
                       output->directions.push_back(Direction::kBackwards);
                     }})
        .Insert(L'j', {.handler = [output](wchar_t) {
                  output->directions.push_back(Direction::kForwards);
                }});
  }

  return cmap.Execute(c);
}

bool ReceiveInput(CommandSetShell* output, wint_t c, State*) {
  if (c == '\n' || static_cast<int>(c) == Terminal::ESCAPE) return false;
  if (static_cast<int>(c) == Terminal::BACKSPACE) {
    if (output->input.empty()) return false;
    output->input.pop_back();
    return true;
  }
  output->input.push_back(c);
  return true;
}

class OperationMode : public EditorMode {
 public:
  OperationMode(TopCommand top_command, EditorState& editor_state)
      : editor_state_(editor_state),
        state_(editor_state, std::move(top_command)) {}

  void ProcessInput(wint_t c) override {
    editor_state_.status().Reset();
    if (!state_.empty() &&
        std::visit([&](auto& t) { return ReceiveInput(&t, c, &state_); },
                   state_.GetLastCommand())) {
      if (state_.empty()) PushDefault();
      state_.Update();
      ShowStatus();
      return;
    }

    switch (static_cast<int>(c)) {
      case L'\n':
        state_.Commit();
        return;
      case Terminal::BACKSPACE:
        state_.UndoLast();
        ShowStatus();
        return;
    }

    PushDefault();
    if (std::visit([&](auto& t) { return ReceiveInput(&t, c, &state_); },
                   state_.GetLastCommand())) {
      state_.Update();
      ShowStatus();
      return;
    }
    if (ReceiveInputTopCommand(state_.top_command(), c)) {
      ShowStatus();
      return;
    }
    // Unhandled character.
    if (static_cast<int>(c) == Terminal::ESCAPE) {
      if (state_.top_command().post_transformation_behavior ==
          transformation::Stack::kNone) {
        state_.Abort();
      } else {
        TopCommand top_command = state_.top_command();
        top_command.post_transformation_behavior = transformation::Stack::kNone;
        state_.set_top_command(std::move(top_command));
      }
      return;
    }
    state_.UndoLast();  // The one we just pushed a few lines above.
    EditorState& editor_state = editor_state_;
    state_.Commit();
    editor_state.ProcessInput(c);
  }

  CursorMode cursor_mode() const override { return CursorMode::kDefault; }

  void ShowStatus() {
    // TODO(easy, 2023-09-08): Change ToStatus to return LazyString.
    editor_state_.status().SetInformationText(
        Append(NewLazyString(ToStatus(state_.top_command())),
               NewLazyString(L":"), NewLazyString(state_.GetStatusString())));
  }

  void PushDefault() { PushCommand(CommandReach()); }

  void PushCommand(Command command) { state_.Push(std::move(command)); }

 private:
  bool ReceiveInputTopCommand(TopCommand top_command, wint_t t) {
    using PTB = transformation::Stack::PostTransformationBehavior;
    switch (static_cast<int>(t)) {
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
      case L'~':
        switch (top_command.post_transformation_behavior) {
          case PTB::kCapitalsSwitch:
            top_command.post_transformation_behavior = PTB::kNone;
            break;
          default:
            top_command.post_transformation_behavior = PTB::kCapitalsSwitch;
            break;
        }
        state_.set_top_command(top_command);
        return true;
      case L'$':
        switch (top_command.post_transformation_behavior) {
          case PTB::kCommandSystem:
            top_command.post_transformation_behavior = PTB::kCommandCpp;
            break;
          case PTB::kCommandCpp:
            top_command.post_transformation_behavior = PTB::kNone;
            break;
          default:
            top_command.post_transformation_behavior = PTB::kCommandSystem;
            break;
        }
        state_.set_top_command(top_command);
        return true;
      case L'|':
        state_.Push(CommandSetShell{});
        return true;
      case L'+':
        switch (top_command.post_transformation_behavior) {
          case PTB::kCursorOnEachLine:
            top_command.post_transformation_behavior = PTB::kNone;
            break;
          default:
            top_command.post_transformation_behavior = PTB::kCursorOnEachLine;
        }
        state_.set_top_command(top_command);
        return true;
      case L'f':
        state_.Push(CommandReachQuery{});
        return true;
      case Terminal::PAGE_DOWN:
      case Terminal::PAGE_UP:
        if (CommandReach* reach =
                state_.empty()
                    ? nullptr
                    : std::get_if<CommandReach>(&state_.GetLastCommand());
            reach != nullptr && reach->structure == std::nullopt) {
          state_.UndoLast();
        }
        state_.Push(CommandReachPage{
            .repetitions = operation::CommandArgumentRepetitions(
                static_cast<int>(t) == Terminal::PAGE_UP ? -1 : 1)});
        return true;
      case L'j':
      case L'k':
        if (CommandReach* reach =
                state_.empty()
                    ? nullptr
                    : std::get_if<CommandReach>(&state_.GetLastCommand());
            reach != nullptr && reach->structure == std::nullopt) {
          state_.UndoLast();
        }
        state_.Push(CommandReachLine{
            .repetitions =
                operation::CommandArgumentRepetitions(t == L'k' ? -1 : 1)});
        return true;
      case L'H':
        state_.Push(CommandReachBegin{});
        return true;
      case L'L':
        state_.Push(CommandReachBegin{.direction = Direction::kBackwards});
        return true;
      case L'K':
        state_.Push(CommandReachBegin{.structure = Structure::kLine});
        return true;
      case L'J':
        state_.Push(CommandReachBegin{.structure = Structure::kLine,
                                      .direction = Direction::kBackwards});
        return true;
    }
    return false;
  }

  static std::wstring ToStatus(TopCommand top_command) {
    switch (top_command.post_transformation_behavior) {
      case transformation::Stack::PostTransformationBehavior::kNone:
        return L"🦋 Move";
      case transformation::Stack::PostTransformationBehavior::kDeleteRegion:
        return L"✂️  Delete";
      case transformation::Stack::PostTransformationBehavior::kCopyRegion:
        return L"📋 Copy";
      case transformation::Stack::PostTransformationBehavior::kCommandSystem:
        return L"🐚 System";
      case transformation::Stack::PostTransformationBehavior::kCommandCpp:
        return L"🤖 Cpp";
      case transformation::Stack::PostTransformationBehavior::kCapitalsSwitch:
        return L" Aa";
      case transformation::Stack::PostTransformationBehavior::kCursorOnEachLine:
        return L"Ꮖ Cursor";
    }
    LOG(FATAL) << "Invalid post transformation behavior.";
    return L"Move";
  }

  EditorState& editor_state_;
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

NonNull<std::unique_ptr<afc::editor::Command>> NewTopLevelCommand(
    std::wstring, std::wstring description, TopCommand top_command,
    EditorState& editor_state, std::vector<Command> commands) {
  return NewSetModeCommand({.editor_state = editor_state,
                            .description = description,
                            .category = L"Edit",
                            .factory = [top_command, &editor_state, commands] {
                              auto output = std::make_unique<OperationMode>(
                                  top_command, editor_state);
                              if (commands.empty()) {
                                output->PushDefault();
                              } else {
                                for (auto& c : commands) {
                                  output->PushCommand(c);
                                }
                              }
                              output->ShowStatus();
                              return output;
                            }});
}

}  // namespace afc::editor::operation
