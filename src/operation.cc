#include "src/operation.h"

#include <memory>
#include <ranges>

#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/file_link_mode.h"
#include "src/find_mode.h"
#include "src/futures/futures.h"
#include "src/futures/serializer.h"
#include "src/goto_command.h"
#include "src/key_commands_map.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/trim.h"
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/operation_bisect.h"
#include "src/operation_find_local.h"
#include "src/operation_scope.h"
#include "src/set_mode_command.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/transformation_composite.h"
#include "src/transformation_delete.h"
#include "src/transformation_move.h"
#include "src/transformation_noop.h"
#include "src/transformation_paste.h"
#include "src/transformation_stack.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::screen::Color;
using afc::infrastructure::screen::StandardColor;
using afc::infrastructure::screen::Style;
using afc::infrastructure::screen::StyleAttribute;
using afc::infrastructure::screen::VisualOverlayMap;
using afc::language::EmptyValue;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::VisitOptional;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::language::lazy_string::TrimLeft;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LinePartMetadata;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor::operation {
using ::operator<<;
namespace commands {
void SerializeCall(NonEmptySingleLine name, std::vector<SingleLine> arguments,
                   LineBuilder& output) {
  output.AppendString(name.read(),
                      LinePartMetadata{.style = Style{StandardColor::Cyan}});
  output.AppendString(
      SingleLine::Char<L'('>(),
      LinePartMetadata{.style = Style{.attributes = StyleAttribute::Dim}});
  SingleLine separator;
  std::ranges::for_each(
      arguments | std::views::filter(std::not_fn(&SingleLine::empty)),
      [&](const SingleLine& a) {
        output.AppendString(
            separator, LinePartMetadata{
                           .style = Style{.attributes = StyleAttribute::Dim}});
        output.AppendString(a, std::nullopt);
        separator = SINGLE_LINE_CONSTANT(L", ");
      });
  output.AppendString(
      SingleLine::Char<L')'>(),
      LinePartMetadata{.style = Style{.attributes = StyleAttribute::Dim}});
}

NonEmptySingleLine StructureToString(std::optional<Structure> structure) {
  return structure
      .transform([](Structure s) { return ToNonEmptySingleLine(s); })
      .value_or(NON_EMPTY_SINGLE_LINE_CONSTANT(L"?"));
}
}  // namespace commands
namespace {
using UndoCallback = std::function<futures::Value<EmptyValue>()>;

Modifiers GetModifiers(std::optional<Structure> structure, int repetitions,
                       Direction direction) {
  return Modifiers{
      .structure = structure.value_or(Structure::Char),
      .direction = repetitions < 0 ? ReverseDirection(direction) : direction,
      .repetitions = abs(repetitions)};
}

Modifiers GetModifiers(std::optional<Structure> structure,
                       const CommandArgumentRepetitions& repetitions,
                       Direction direction) {
  return GetModifiers(structure, repetitions.get(), direction);
}

static const Description kMoveDown =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🧗👇")};
static const Description kMoveUp =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🧗👆")};
static const Description kPageDown =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"📜👇")};
static const Description kPageUp =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"📜👆")};
static const Description kMoveLeft =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👈")};
static const Description kMoveRight =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👉")};
static const Description kHomeLeft =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👈")};
static const Description kHomeRight =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👉")};
static const Description kHomeUp =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👆")};
static const Description kHomeDown =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🏠👇")};
static const Description kDescriptionShell =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🌀")};
static const Description kDescriptionPaste =
    Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"📎")};

void AppendStatus(const CommandReach& reach, LineBuilder& output) {
  commands::SerializeCall(NON_EMPTY_SINGLE_LINE_CONSTANT(L"🦀"),
                          {commands::StructureToString(reach.structure).read(),
                           reach.repetitions.ToString()},
                          output);
}

void AppendStatus(const CommandReachBegin& reach, LineBuilder& output) {
  commands::SerializeCall(
      (reach.direction == Direction::Backwards
           ? (reach.structure == Structure::Line ? kHomeUp : kHomeRight)
           : (reach.structure == Structure::Line ? kHomeDown : kHomeLeft))
          .read(),
      {commands::StructureToString(reach.structure).read(),
       reach.repetitions.ToString()},
      output);
}

void AppendStatus(const CommandReachLine& reach_line, LineBuilder& output) {
  commands::SerializeCall(
      reach_line.repetitions.get() >= 0 ? kMoveDown.read() : kMoveUp.read(),
      {reach_line.repetitions.ToString()}, output);
}

void AppendStatus(const CommandReachPage& reach_line, LineBuilder& output) {
  commands::SerializeCall(
      reach_line.repetitions.get() >= 0 ? kPageDown.read() : kPageUp.read(),
      {reach_line.repetitions.ToString()}, output);
}

void AppendStatus(const CommandSetShell& c, LineBuilder& output) {
  commands::SerializeCall(kDescriptionShell.read(), {c.input}, output);
}

void AppendStatus(const CommandPaste& paste, LineBuilder& output) {
  commands::SerializeCall(
      kDescriptionPaste.read(),
      std::vector<SingleLine>{paste.repetitions.ToString(),
                              paste.query_input.has_value()
                                  ? SINGLE_LINE_CONSTANT(L"\"") +
                                        paste.query_input.value() +
                                        SINGLE_LINE_CONSTANT(L"\"")
                                  : SingleLine{}},
      output);
}

void AppendStatus(const NonNull<std::shared_ptr<MoveOperationCommand>>& op,
                  LineBuilder& output) {
  return output.Append(op->status());
}

futures::Value<UndoCallback> ExecuteTransformation(
    EditorState& editor, ApplicationType application_type,
    transformation::Variant transformation) {
  TRACK_OPERATION(Operation_ExecuteTransformation);

  auto buffers_transformed =
      std::make_shared<std::vector<gc::Root<OpenBuffer>>>();
  return editor
      .ForEachActiveBuffer([transformation = std::move(transformation),
                            buffers_transformed,
                            application_type](OpenBuffer& buffer) {
        TRACK_OPERATION(ExecuteTransformation_ApplyTransformation);
        buffers_transformed->push_back(buffer.RootFromThis());
        return buffer.ApplyToCursors(
            transformation,
            buffer.Read(buffer_variables::multiple_cursors)
                ? Modifiers::CursorsAffected::kAll
                : Modifiers::CursorsAffected::kOnlyCurrent,
            application_type == ApplicationType::Preview
                ? transformation::Input::Mode::Preview
                : transformation::Input::Mode::Final);
      })
      .Transform([buffers_transformed](EmptyValue) {
        return UndoCallback([buffers_transformed] {
          TRACK_OPERATION(ExecuteTransformation_Undo);
          return futures::ForEach(
                     buffers_transformed->begin(), buffers_transformed->end(),
                     [buffers_transformed](gc::Root<OpenBuffer> buffer) {
                       return buffer.ptr()
                           ->Undo(UndoState::ApplyOptions::Mode::kOnlyOne,
                                  UndoState::ApplyOptions::RedoMode::kIgnore)
                           .Transform([](auto) {
                             return futures::IterationControlCommand::Continue;
                           });
                     })
              .Transform([](auto) { return EmptyValue(); });
        });
      });
}

transformation::Stack ApplyRepetitions(
    const CommandArgumentRepetitions& repetitions,
    std::optional<Structure> structure,
    language::NonNull<std::shared_ptr<CompositeTransformation>>
        inner_transformation) {
  transformation::Stack output;
  std::ranges::copy(
      repetitions.get_list() |
          std::views::transform([&](int repetitions_value) {
            return transformation::ModifiersAndComposite{
                .modifiers = GetModifiers(structure, repetitions_value,
                                          Direction::Forwards),
                .transformation = inner_transformation};
          }),
      std::back_inserter(output));
  return output;
}

namespace {
bool apply_repetitions_test = tests::Register(
    L"operation::ApplyRepetitions",
    std::vector<tests::Test>(
        {{.name = L"Empty",
          .callback =
              [] {
                NonNull<std::shared_ptr<OperationScope>> operation_scope;
                LOG(INFO) << ToString(ApplyRepetitions(
                    CommandArgumentRepetitions(1), Structure::Line,
                    NewMoveTransformation(operation_scope)));
              }},
         {.name = L"LongRepetitionsList", .callback = [] {
            NonNull<std::shared_ptr<OperationScope>> operation_scope;
            CommandArgumentRepetitions repetitions(1);
            repetitions.sum(1);
            repetitions.sum(-1);
            repetitions.sum(1);
            repetitions.sum(-1);
            LOG(INFO) << ToString(
                ApplyRepetitions(repetitions, Structure::Line,
                                 NewMoveTransformation(operation_scope)));
          }}}));
}  // namespace

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
    transformation::Stack&, CommandReach reach) {
  return ApplyRepetitions(reach.repetitions, reach.structure,
                          NewMoveTransformation(operation_scope));
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
  return ApplyRepetitions(reach_line.repetitions, Structure::Line,
                          NewMoveTransformation(operation_scope));
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
    transformation::Stack&, CommandReachPage reach_page) {
  return ApplyRepetitions(reach_page.repetitions, Structure::Page,
                          NewMoveTransformation(operation_scope));
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>&,
    transformation::Stack& stack, CommandSetShell shell) {
  stack.post_transformation_behavior =
      transformation::Stack::PostTransformationBehavior::CommandSystem;
  stack.shell = transformation::ShellCommand(shell.input.read());
  return transformation::Stack{};
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>&, transformation::Stack&,
    CommandPaste paste) {
  return ApplyRepetitions(
      paste.repetitions, std::nullopt,
      MakeNonNullShared<transformation::Paste>(
          transformation::Paste{FindFragmentQuery{
              .filter = paste.query_input.value_or(SingleLine{})}}));
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& scope,
    transformation::Stack& stack,
    NonNull<std::shared_ptr<MoveOperationCommand>>& op) {
  return op->GetTransformation(scope, stack);
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
    TRACK_OPERATION(State_Push);
    commands_.push_back(command);
    Update(ApplicationType::Preview);
  }

  void AppendStatusString(LineBuilder& output) const {
    for (const auto& op : commands_) {
      output.AppendString(SingleLine::Char<L' '>(), std::nullopt);
      std::visit([&output](auto& t) { AppendStatus(t, output); }, op);
    }
  }

  void Abort() {
    RunUndoCallback();
    editor_state_.set_keyboard_redirect(std::nullopt);
  }

  void Update() { Update(ApplicationType::Preview); }

  void Commit() {
    TRACK_OPERATION(Operation_State_Commit);
    // We make a copy because Update may delete us.
    EditorState& editor_state = editor_state_;
    Update(ApplicationType::Commit);
    editor_state.set_keyboard_redirect(std::nullopt);
  }

  void RunUndoCallback() {
    TRACK_OPERATION(State_RunUndoCallback);
    const EditorState& editor = editor_state_;
    const std::optional<gc::Root<InputReceiver>> keyboard_redirect =
        editor.keyboard_redirect();
    serializer_.Push([callback = std::move(undo_callback_)]() {
      return Pointer(callback).Reference()();
    });
    CHECK(keyboard_redirect == editor.keyboard_redirect())
        << "Internal error: undo callback has changed the keyboard redirector, "
           "probably causing us to be deleted. This isn't supported (as this "
           "code assumes survival of various now-deleted objects).";

    undo_callback_ = std::make_shared<UndoCallback>(
        []() -> futures::Value<EmptyValue> { return EmptyValue{}; });
  }

  void UndoLast() {
    TRACK_OPERATION(State_UndoLast);
    commands_.pop_back();
    if (commands_.empty()) Push(CommandReach());
    RunUndoCallback();
    Update();
  }

  futures::Value<gc::Root<OpenBuffer>> GetHelpBuffer() {
    return VisitOptional(
        [](gc::Root<OpenBuffer> buffer)
            -> futures::Value<gc::Root<OpenBuffer>> {
          buffer.ptr()->Reload();
          return buffer;
        },
        [this] {
          return OpenAnonymousBuffer(editor_state_)
              .Transform([storage = help_buffer_](gc::Root<OpenBuffer> buffer) {
                buffer.ptr()->Set(buffer_variables::paste_mode, true);
                storage.value() = buffer;
                return buffer;
              });
        },
        help_buffer_.value());
  }

 private:
  futures::Value<EmptyValue> Update(ApplicationType application_type) {
    TRACK_OPERATION(Operation_State_Update);
    CHECK(!commands_.empty());
    RunUndoCallback();
    std::shared_ptr<UndoCallback> original_undo_callback = undo_callback_;
    return StartTransformationExecution(application_type, PrepareStack())
        .Transform([original_undo_callback](UndoCallback undo_callback) {
          *original_undo_callback =
              [previous = std::move(*original_undo_callback), undo_callback]() {
                return undo_callback().Transform(
                    [previous](EmptyValue) { return previous(); });
              };
          return EmptyValue();
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
      if (separator.has_value()) stack.push_back(separator.value());
      stack.push_back(std::visit(
          [&](auto t) -> transformation::Variant {
            TRACK_OPERATION(State_PrepareStack_GetTransformation);
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
    TRACK_OPERATION(Operation_State_StartTransformationExecution);
    futures::Future<UndoCallback> output;
    serializer_.Push([&editor_state = editor_state_, application_type,
                      consumer = std::move(output.consumer),
                      transformation] mutable {
      return ExecuteTransformation(editor_state, application_type,
                                   transformation)
          .Transform([consumer = std::move(consumer)](
                         UndoCallback undo_callback) mutable {
            std::move(consumer)(std::move(undo_callback));
            return EmptyValue{};
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
      []() -> futures::Value<EmptyValue> { return EmptyValue{}; });

  // If we've needed an anonymous buffer to show help, retains it here.
  //
  // The main reason to do this is to avoid flickering while the buffer is shown
  // (which would otherwise be caused by having to create a new buffer on each
  // key press).
  //
  // std::shared_ptr<> so that it can be captured by the callback in
  // `GetAnonymousBuffer` and survive if `this` is deleted.
  NonNull<std::shared_ptr<std::optional<gc::Root<OpenBuffer>>>> help_buffer_ =
      MakeNonNullShared<std::optional<gc::Root<OpenBuffer>>>();
};

std::optional<CommandArgumentRepetitions*> GetRepetitions(Command& command) {
  using Ret = std::optional<CommandArgumentRepetitions*>;
  return std::visit(
      overload{[](CommandReach& c) -> Ret { return &c.repetitions; },
               [](CommandReachBegin& c) -> Ret { return &c.repetitions; },
               [](CommandReachLine& c) -> Ret { return &c.repetitions; },
               [](CommandReachPage& c) -> Ret { return &c.repetitions; },
               [](auto) -> Ret { return std::nullopt; }},
      command);
}

const std::unordered_map<wchar_t, Structure>& structure_bindings() {
  static const std::unordered_map<wchar_t, Structure> output = {
      {L'z', Structure::Char},      {L'x', Structure::Word},
      {L'c', Structure::Symbol},    {L'v', Structure::Line},
      {L'b', Structure::Paragraph}, {L'n', Structure::Page},
      {L'm', Structure::Buffer},    {L'C', Structure::Cursor},
      {L'V', Structure::Tree}};
  return output;
}

void CheckStructureChar(KeyCommandsMap& cmap,
                        std::optional<Structure>* structure,
                        CommandArgumentRepetitions* repetitions) {
  CHECK(structure != nullptr);
  CHECK(repetitions != nullptr);

  for (const std::pair<const wchar_t, Structure>& entry :
       structure_bindings()) {
    VLOG(9) << "Add key: " << entry.second;
    NonEmptySingleLine structure_name =
        commands::StructureToString(entry.second);
    cmap.Insert(entry.first,
                {.category = KeyCommandsMap::Category::Structure,
                 .description = Description{structure_name},
                 .active = *structure == std::nullopt,
                 .handler =
                     [structure, repetitions, &entry](ExtendedChar) {
                       LOG(INFO) << "Running, storing: " << entry.second;
                       *structure = entry.second;
                       if (repetitions->get() == 0) {
                         repetitions->sum(1);
                       }
                     }})
        .Insert(entry.first, {.category = KeyCommandsMap::Category::Structure,
                              .description = Description{structure_name},
                              .active = entry.second == *structure,
                              .handler = [repetitions](ExtendedChar) {
                                repetitions->sum(1);
                              }});
  };
}

void CheckIncrementsChar(KeyCommandsMap& cmap,
                         CommandArgumentRepetitions* output) {
  cmap.Insert(L'h', {.category = KeyCommandsMap::Category::Repetitions,
                     .description = kMoveLeft,
                     .handler = [output](ExtendedChar) { output->sum(-1); }})
      .Insert(ControlChar::LeftArrow,
              {.category = KeyCommandsMap::Category::Repetitions,
               .description = kMoveLeft,
               .handler = [output](ExtendedChar) { output->sum(-1); }})
      .Insert(L'l', {.category = KeyCommandsMap::Category::Repetitions,
                     .description = kMoveRight,
                     .handler = [output](ExtendedChar) { output->sum(1); }})
      .Insert(ControlChar::RightArrow,
              {.category = KeyCommandsMap::Category::Repetitions,
               .description = kMoveRight,
               .handler = [output](ExtendedChar) { output->sum(1); }});
}

void CheckRepetitionsChar(KeyCommandsMap& cmap,
                          CommandArgumentRepetitions* output) {
  cmap.Insert(
      ControlChar::Backspace,
      {.category = KeyCommandsMap::Category::StringControl,
       .description =
           Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"PopRepetitions")},
       .active = !output->empty(),
       .handler = [output](ExtendedChar) { output->PopValue(); }});
  for (int i = 0; i < 10; i++)
    cmap.Insert(
        L'0' + i,
        {.category = KeyCommandsMap::Category::Repetitions,
         .description =
             Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Repetitions")},
         .handler = [output, i](ExtendedChar) { output->factor(i); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReach* output,
                       State* state) {
  if (output->structure.value_or(Structure::Char) == Structure::Char &&
      !output->repetitions.empty()) {
    cmap.Insert(L'H', {.category = KeyCommandsMap::Category::NewCommand,
                       .description = commands::BisectLeftDescription(),
                       .active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](ExtendedChar) {
                             state->Push(Bisect(commands::BisectOptions{
                                 .structure = Structure::Char,
                                 .directions = {Direction::Backwards}}));
                           }})
        .Insert(L'L', {.category = KeyCommandsMap::Category::NewCommand,
                       .description = commands::BisectRightDescription(),
                       .active = output->repetitions.get_list().back() > 0,
                       .handler = [state](ExtendedChar) {
                         state->Push(Bisect(commands::BisectOptions{
                             .structure = Structure::Char,
                             .directions = {Direction::Forwards}}));
                       }});
  }

  if (output->structure == Structure::Line && !output->repetitions.empty()) {
    cmap.Insert(L'K', {.category = KeyCommandsMap::Category::NewCommand,
                       .description = commands::BisectUpDescription(),
                       .active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](ExtendedChar) {
                             state->Push(Bisect(commands::BisectOptions{
                                 .structure = Structure::Line,
                                 .directions = {Direction::Backwards}}));
                           }})
        .Insert(L'J', {.category = KeyCommandsMap::Category::NewCommand,
                       .description = commands::BisectDownDescription(),
                       .active = output->repetitions.get_list().back() > 0,
                       .handler = [state](ExtendedChar) {
                         state->Push(Bisect(commands::BisectOptions{
                             .structure = Structure::Line,
                             .directions = {Direction::Forwards}}));
                       }});
  }

  CheckStructureChar(cmap, &output->structure, &output->repetitions);
  CheckIncrementsChar(cmap, &output->repetitions);
  CheckRepetitionsChar(cmap, &output->repetitions);
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachBegin* output,
                       State*) {
  if (output->structure == Structure::Line) {
    auto handler = [&](Description description) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::Repetitions,
          .description = description,
          .handler = [output](ExtendedChar t) {
            int delta = (t == ExtendedChar(L'j') ||
                         t == ExtendedChar(ControlChar::DownArrow))
                            ? 1
                            : -1;
            if (output->direction == Direction::Backwards) {
              delta *= -1;
            }
            output->repetitions.sum(delta);
          }};
    };
    cmap.Insert(L'j',
                handler(Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👇")}))
        .Insert(ControlChar::DownArrow,
                handler(Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👇")}))
        .Insert(L'k',
                handler(Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👆")}))
        .Insert(ControlChar::UpArrow,
                handler(Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"👆")}));
  }

  CheckStructureChar(cmap, &output->structure, &output->repetitions);
  CheckIncrementsChar(cmap, &output->repetitions);
  CheckRepetitionsChar(cmap, &output->repetitions);

  if (output->structure.value_or(Structure::Char) == Structure::Char ||
      output->structure == Structure::Line) {
    // Don't let CheckRepetitionsChar below handle these; we'd
    // rather preserve the usual meaning (of scrolling by a
    // character).
    cmap.Erase(L'h').Erase(L'l');
  }
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachLine* output,
                       State* state) {
  cmap.Insert(L'K', {.category = KeyCommandsMap::Category::NewCommand,
                     .description =
                         Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👆")},
                     .active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() < 0,
                     .handler =
                         [state](ExtendedChar) {
                           state->Push(Bisect(commands::BisectOptions{
                               .structure = Structure::Line,
                               .directions = {Direction::Backwards}}));
                         }})
      .Insert(L'J', {.category = KeyCommandsMap::Category::NewCommand,
                     .description =
                         Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"🪓👇")},
                     .active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() > 0,
                     .handler = [state](ExtendedChar) {
                       state->Push(Bisect(commands::BisectOptions{
                           .structure = Structure::Line,
                           .directions = {Direction::Forwards}}));
                     }});

  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(
          L'j',
          {.category = KeyCommandsMap::Category::Repetitions,
           .description = kMoveDown,
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(
          ControlChar::DownArrow,
          {.category = KeyCommandsMap::Category::Repetitions,
           .description = kMoveDown,
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(
          L'k',
          {.category = KeyCommandsMap::Category::Repetitions,
           .description = kMoveUp,
           .handler = [output](ExtendedChar) { output->repetitions.sum(-1); }})
      .Insert(
          ControlChar::UpArrow,
          {.category = KeyCommandsMap::Category::Repetitions,
           .description = kMoveUp,
           .handler = [output](ExtendedChar) { output->repetitions.sum(-1); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachPage* output, State*) {
  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(
          ControlChar::PageDown,
          {.category = KeyCommandsMap::Category::NewCommand,
           .description = kPageDown,
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(
          ControlChar::PageUp,
          {.category = KeyCommandsMap::Category::NewCommand,
           .description = kPageUp,
           .handler = [output](ExtendedChar) { output->repetitions.sum(-1); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandSetShell* output, State*) {
  cmap.Insert(ControlChar::Backspace,
              {.category = KeyCommandsMap::Category::StringControl,
               .description =
                   Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Backspace")},
               .active = !output->input.empty(),
               .handler =
                   [output](ExtendedChar) {
                     output->input = output->input.Substring(
                         ColumnNumber{0},
                         output->input.size() - ColumnNumberDelta{1});
                   }})
      .SetFallback({'\n', ControlChar::Escape, ControlChar::Backspace},
                   [output](ExtendedChar extended_c) {
                     std::visit(overload{[](ControlChar) {},
                                         [&](wchar_t c) {
                                           output->input +=
                                               SingleLine{LazyString{
                                                   ColumnNumberDelta{1}, c}};
                                         }},
                                extended_c);
                   });
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandPaste* output, State*) {
  if (output->query_input.has_value()) {
    cmap.SetFallback(
        {'\n', ControlChar::Escape}, [output](ExtendedChar extended_c) {
          std::visit(
              overload{
                  [output](ControlChar c) {
                    switch (c) {
                      case ControlChar::Backspace:
                        if (output->query_input->empty())
                          output->query_input = std::nullopt;
                        else
                          output->query_input = output->query_input->Substring(
                              ColumnNumber{}, output->query_input->size() -
                                                  ColumnNumberDelta{1});
                        // TODO(trivial, 2024-09-16): Handle more
                        // control characters.
                      default:
                        break;
                    }
                  },
                  [&](wchar_t c) {
                    CHECK(c != L'\n');  // Exempted above (in cmap.SetFallback).
                    output->query_input =
                        output->query_input.value() +
                        SingleLine{LazyString{ColumnNumberDelta{1}, c}};
                  }},
              extended_c);
        });
    return;
  }
  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(
          L'p',
          {.category = KeyCommandsMap::Category::Repetitions,
           .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Paste")},
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(L'f', {.category = KeyCommandsMap::Category::StringControl,
                     .description =
                         Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Filter")},
                     .handler = [output](ExtendedChar) {
                       CHECK(!output->query_input.has_value());
                       output->query_input = SingleLine{};
                     }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap,
                       NonNull<std::shared_ptr<MoveOperationCommand>>* output,
                       State*) {
  cmap = (*output)->key_commands_map();
}

class OperationMode : public SimpleInputReceiver {
 public:
  OperationMode(TopCommand top_command, EditorState& editor_state)
      : editor_state_(editor_state),
        state_(editor_state, std::move(top_command)) {}

  void ProcessInput(ExtendedChar c) override {
    editor_state_.status().Reset();
    GetGlobalKeyCommandsMap().Execute(c);
  }

  CursorMode cursor_mode() const override { return CursorMode::Default; }

  void Expand(gc::ObjectMetadata::Receiver&) const override {}

  void ShowStatus() {
    LineBuilder output;
    AppendStatus(state_.top_command(), output);
    output.AppendString(
        SingleLine::Char<L':'>(),
        LinePartMetadata{.style = Style{.attributes = StyleAttribute::Dim}});
    state_.AppendStatusString(output);
    AppendStatusForCommandsAvailable(output);
    editor_state_.status().SetInformationText(std::move(output).Build());
    if (state_.top_command().show_help) {
      LineSequence help = GetGlobalKeyCommandsMap().Help();
      state_.GetHelpBuffer().Transform(
          [&editor_state = editor_state_, help](gc::Root<OpenBuffer> context) {
            context.ptr()->InsertInPosition(help, LineColumn(), std::nullopt);
            editor_state.status().set_context(context);
            return Success();
          });
    }
  }

  void PushCommand(Command command) { state_.Push(std::move(command)); }

 private:
  KeyCommandsMapSequence GetGlobalKeyCommandsMap() {
    KeyCommandsMapSequence cmap;

    if (!state_.empty()) {
      std::visit(
          [&](auto& t) {
            GetKeyCommandsMap(cmap.PushNew().OnHandle([this] {
              if (state_.empty()) PushCommand(CommandReach{});
              state_.Update();
              ShowStatus();
            }),
                              &t, &state_);
          },
          state_.GetLastCommand());
    }

    cmap.PushNew()
        .Insert(L'\n',
                {.category = KeyCommandsMap::Category::Top,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Apply")},
                 .handler = [this](ExtendedChar) { state_.Commit(); }})
        .Insert(ControlChar::Backspace,
                {.category = KeyCommandsMap::Category::StringControl,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Backspace")},
                 .handler = [this](ExtendedChar) {
                   state_.UndoLast();
                   ShowStatus();
                 }});

    KeyCommandsMap& structure_keys = cmap.PushNew();
    for (const std::pair<const wchar_t, Structure>& entry :
         structure_bindings())
      structure_keys.Insert(
          entry.first,
          {.category = KeyCommandsMap::Category::Structure,
           .description =
               Description{commands::StructureToString(entry.second)},
           .handler = [this, structure = entry.second](ExtendedChar) {
             int last_repetitions = 0;
             if (!state_.empty()) {
               if (const std::optional<CommandArgumentRepetitions*>
                       repetitions = GetRepetitions(state_.GetLastCommand());
                   repetitions.has_value() && !(*repetitions)->empty()) {
                 last_repetitions = (*repetitions)->get_list().back();
               }
             }
             state_.Push(CommandReach{
                 .structure = structure,
                 .repetitions = last_repetitions < 0
                                    ? -1
                                    : (last_repetitions > 0 ? 1 : 0)});
           }});

    structure_keys
        .Insert(L'h',
                {.category = KeyCommandsMap::Category::NewCommand,
                 .description = kMoveLeft,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::Char,
                                                .repetitions = -1});
                     }})
        .Insert(ControlChar::LeftArrow,
                {.category = KeyCommandsMap::Category::NewCommand,
                 .description = kMoveLeft,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::Char,
                                                .repetitions = -1});
                     }})
        .Insert(L'l',
                {.category = KeyCommandsMap::Category::NewCommand,
                 .description = kMoveRight,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::Char,
                                                .repetitions = 1});
                     }})
        .Insert(ControlChar::RightArrow,
                {.category = KeyCommandsMap::Category::NewCommand,
                 .description = kMoveRight,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::Char,
                                                .repetitions = 1});
                     }})
        .OnHandle([this] {
          state_.Update();
          ShowStatus();
        });

    cmap.PushBack(ReceiveInputTopCommand(state_.top_command()));

    // Unhandled character.
    cmap.PushNew()
        .Insert(ControlChar::Escape,
                {.category = KeyCommandsMap::Category::StringControl,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Cancel")},
                 .handler =
                     [&state = state_](ExtendedChar) {
                       if (state.top_command().post_transformation_behavior ==
                           transformation::Stack::PostTransformationBehavior::
                               None) {
                         state.Abort();
                       } else {
                         TopCommand top_command = state.top_command();
                         top_command.post_transformation_behavior =
                             transformation::Stack::PostTransformationBehavior::
                                 None;
                         state.set_top_command(std::move(top_command));
                       }
                     }})
        .SetFallback({}, [&state = state_,
                          &editor_state = editor_state_](ExtendedChar c) {
          state.Commit();
          editor_state.ProcessInput({c});
        });
    return cmap;
  }

  void AppendStatusForCommandsAvailable(LineBuilder& output) {
    output.AppendString(SingleLine::Padding(ColumnNumberDelta{4}),
                        std::nullopt);
    output.Append(GetGlobalKeyCommandsMap().SummaryLine());
  }

  KeyCommandsMap ReceiveInputTopCommand(TopCommand top_command) {
    using PTB = transformation::Stack::PostTransformationBehavior;
    auto push = [&state = state_](Description description, Command value) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::NewCommand,
          .description = description,
          .handler = [&state, value](ExtendedChar) { state.Push(value); }};
    };
    auto PageHandler = [&](ControlChar c) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::NewCommand,
          .description = c == ControlChar::PageUp ? kPageUp : kPageDown,
          .handler = [&state = state_, c](ExtendedChar) {
            if (CommandReach* reach =
                    state.empty()
                        ? nullptr
                        : std::get_if<CommandReach>(&state.GetLastCommand());
                reach != nullptr && reach->structure == std::nullopt) {
              state.UndoLast();
            }
            state.Push(CommandReachPage{
                .repetitions = operation::CommandArgumentRepetitions(
                    c == ControlChar::PageUp ? -1 : 1)});
          }};
    };
    auto MoveHandler = [&](ExtendedChar c) {
      CHECK(c == ExtendedChar(L'j') || c == ExtendedChar(L'k') ||
            c == ExtendedChar(ControlChar::DownArrow) ||
            c == ExtendedChar(ControlChar::UpArrow));
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::NewCommand,
          .description = (c == ExtendedChar(L'j') ||
                          c == ExtendedChar(ControlChar::DownArrow))
                             ? kMoveDown
                             : kMoveUp,
          .handler = [&state = state_, c](ExtendedChar) {
            if (CommandReach* reach =
                    state.empty()
                        ? nullptr
                        : std::get_if<CommandReach>(&state.GetLastCommand());
                reach != nullptr && reach->structure == std::nullopt &&
                reach->repetitions.empty()) {
              state.UndoLast();
            }
            state.Push(CommandReachLine{
                .repetitions = operation::CommandArgumentRepetitions(
                    c == ExtendedChar(L'k') ||
                            c == ExtendedChar(ControlChar::UpArrow)
                        ? -1
                        : 1)});
          }};
    };
    KeyCommandsMap cmap;
    cmap.OnHandle([this] { ShowStatus(); });
    cmap.Insert(L'd',
                {.category = KeyCommandsMap::Category::Top,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Delete")},
                 .handler =
                     [top_command, &state = state_](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::DeleteRegion:
                           top_command.post_transformation_behavior =
                               PTB::CopyRegion;
                           break;
                         case PTB::CopyRegion:
                           top_command.post_transformation_behavior = PTB::None;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::DeleteRegion;
                           break;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(
            L'p',
            KeyCommandsMap::KeyCommand{
                .category = KeyCommandsMap::Category::NewCommand,
                .description = kDescriptionPaste,
                .active = editor_state_.buffer_registry()
                              .Find(PasteBuffer{})
                              .has_value(),
                .handler = [&state = state_](
                               ExtendedChar) { state.Push(CommandPaste{}); }})
        .Insert(L'?',
                {.category = KeyCommandsMap::Category::Top,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Help")},
                 .handler =
                     [&state = state_, top_command](ExtendedChar) mutable {
                       top_command.show_help = !top_command.show_help;
                       state.set_top_command(top_command);
                     }})
        .Insert(L'~',
                {.category = KeyCommandsMap::Category::Top,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"SwitchCase")},
                 .handler =
                     [top_command, &state = state_](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::CapitalsSwitch:
                           top_command.post_transformation_behavior = PTB::None;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::CapitalsSwitch;
                           break;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'$',
                {.category = KeyCommandsMap::Category::Top,
                 .description =
                     Description{NON_EMPTY_SINGLE_LINE_CONSTANT(L"Shell")},
                 .handler =
                     [top_command, &state = state_](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::CommandSystem:
                           top_command.post_transformation_behavior =
                               PTB::CommandCpp;
                           break;
                         case PTB::CommandCpp:
                           top_command.post_transformation_behavior = PTB::None;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::CommandSystem;
                           break;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'|', push(kDescriptionShell, CommandSetShell{}))
        .Insert(L'+',
                {.category = KeyCommandsMap::Category::Top,
                 .description = Description{NON_EMPTY_SINGLE_LINE_CONSTANT(
                     L"CursorEveryLine")},
                 .handler =
                     [&state = state_, top_command](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::CursorOnEachLine:
                           top_command.post_transformation_behavior = PTB::None;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::CursorOnEachLine;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'f',
                push(commands::FindLocalDescription(), commands::FindLocal()))
        .Insert(ControlChar::PageDown, PageHandler(ControlChar::PageDown))
        .Insert(ControlChar::PageUp, PageHandler(ControlChar::PageUp))
        .Insert(L'j', MoveHandler('j'))
        .Insert(L'k', MoveHandler('k'))
        .Insert(ControlChar::DownArrow, MoveHandler(ControlChar::DownArrow))
        .Insert(ControlChar::UpArrow, MoveHandler(ControlChar::UpArrow))
        .Insert(L'H', push(kHomeLeft, CommandReachBegin{}))
        .Insert(ControlChar::Home, push(kHomeLeft, CommandReachBegin{}))
        .Insert(L'L',
                push(kHomeRight,
                     CommandReachBegin{.direction = Direction::Backwards}))
        .Insert(ControlChar::End,
                push(kHomeRight,
                     CommandReachBegin{.direction = Direction::Backwards}))
        .Insert(L'K',
                push(kHomeUp, CommandReachBegin{.structure = Structure::Line}))
        .Insert(L'J', push(kHomeDown, CommandReachBegin{
                                          .structure = Structure::Line,
                                          .direction = Direction::Backwards}));
    return cmap;
  }

  static void AppendStatus(TopCommand top_command, LineBuilder& output) {
    switch (top_command.post_transformation_behavior) {
      case transformation::Stack::PostTransformationBehavior::None:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"🦋 Move"),
            LinePartMetadata{.style =
                                 Style{.foreground_color = StandardColor::Cyan,
                                       .attributes = StyleAttribute::Bold}});
        return;
      case transformation::Stack::PostTransformationBehavior::DeleteRegion:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"✂️  Delete"),
            LinePartMetadata{.style =
                                 Style{.background_color = StandardColor::Red,
                                       .attributes = StyleAttribute::Bold}});
        return;
      case transformation::Stack::PostTransformationBehavior::CopyRegion:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"📋 Copy"),
            LinePartMetadata{
                .style = Style{.foreground_color = StandardColor::Yellow,
                               .attributes = StyleAttribute::Bold}});
        return;
      case transformation::Stack::PostTransformationBehavior::CommandSystem:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"🐚 System"),
            LinePartMetadata{.style =
                                 Style{.foreground_color = StandardColor::Green,
                                       .attributes = StyleAttribute::Bold}});
        return;
      case transformation::Stack::PostTransformationBehavior::CommandCpp:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"🤖 Cpp"),
            LinePartMetadata{
                .style = Style{.foreground_color = StandardColor::Green,
                               .attributes = StyleAttribute::Bold |
                                             StyleAttribute::Underline}});
        return;
      case transformation::Stack::PostTransformationBehavior::CapitalsSwitch:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"🔠 Aa"),
            LinePartMetadata{
                .style = Style{.foreground_color = StandardColor::Magenta,
                               .attributes = StyleAttribute::Bold}});
        return;
      case transformation::Stack::PostTransformationBehavior::CursorOnEachLine:
        output.AppendString(
            SINGLE_LINE_CONSTANT(L"Ꮖ Cursor"),
            LinePartMetadata{
                .style = Style{.foreground_color = StandardColor::Magenta,
                               .attributes = StyleAttribute::Bold}});
        return;
    }
    LOG(FATAL) << "Invalid post transformation "
                  "behavior.";
  }

  EditorState& editor_state_;
  State state_;
};
}  // namespace

SingleLine CommandArgumentRepetitions::ToString() const {
  return TrimLeft(
      Concatenate(get_list() | std::views::transform([](int r) -> SingleLine {
                    return (r > 0 ? SingleLine::Char<L'+'>() : SingleLine{}) +
                           NonEmptySingleLine(r);
                  })),
      {L'+'});
}

int CommandArgumentRepetitions::get() const {
  return container::Sum(get_list());
}

std::list<int> CommandArgumentRepetitions::get_list() const {
  return container::MaterializeList(
      entries_ | std::views::transform(Flatten) |
      std::views::filter([](int c) { return c != 0; }));
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

gc::Root<afc::editor::Command> NewTopLevelCommand(std::wstring,
                                                  LazyString description,
                                                  TopCommand top_command,
                                                  EditorState& editor_state,
                                                  Command command) {
  return NewSetModeCommand(
      {.editor_state = editor_state,
       .description = description,
       .category = CommandCategory::kEdit(),
       .factory = [top_command, &editor_state, command] {
         auto output =
             MakeNonNullUnique<OperationMode>(top_command, editor_state);
         output->PushCommand(command);
         output->ShowStatus();
         return editor_state.gc_pool().NewRoot<InputReceiver>(
             std::move(output));
       }});
}

}  // namespace afc::editor::operation
