#include "src/operation.h"

#include <memory>
#include <ranges>

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
#include "src/language/overload.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"
#include "src/operation_scope.h"
#include "src/set_mode_command.h"
#include "src/terminal.h"
#include "src/tests/tests.h"
#include "src/transformation/bisect.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/move.h"
#include "src/transformation/noop.h"
#include "src/transformation/reach_query.h"
#include "src/transformation/stack.h"

namespace gc = afc::language::gc;
namespace container = afc::language::container;

using afc::futures::Past;
using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Tracker;
using afc::infrastructure::screen::LineModifier;
using afc::infrastructure::screen::LineModifierSet;
using afc::infrastructure::screen::VisualOverlayMap;
using afc::language::EmptyValue;
using afc::language::FromByteString;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::VisitOptional;
using afc::language::lazy_string::Append;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineBuilder;
using afc::language::text::LineColumn;
using afc::language::text::LineSequence;
using afc::language::text::MutableLineSequence;

namespace afc::editor::operation {
using ::operator<<;
namespace {
using UndoCallback = std::function<futures::Value<EmptyValue>()>;

void SerializeCall(std::wstring name, std::vector<std::wstring> arguments,
                   LineBuilder& output) {
  output.AppendString(name, LineModifierSet{LineModifier::kCyan});
  output.AppendString(L"(", LineModifierSet{LineModifier::kDim});
  std::wstring separator = L"";
  for (auto& a : arguments) {
    if (!a.empty()) {
      output.AppendString(separator, LineModifierSet{LineModifier::kDim});
      output.AppendString(a, std::nullopt);
      separator = L", ";
    }
  }
  output.AppendString(L")", LineModifierSet{LineModifier::kDim});
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

static const Description kMoveDown = Description(L"🧗👇");
static const Description kMoveUp = Description(L"🧗👆");
static const Description kPageDown = Description(L"📜👇");
static const Description kPageUp = Description(L"📜👆");
static const Description kMoveLeft = Description(L"👈");
static const Description kMoveRight = Description(L"👉");
static const Description kHomeLeft = Description(L"🏠👈");
static const Description kHomeRight = Description(L"🏠👉");
static const Description kHomeUp = Description(L"🏠👆");
static const Description kHomeDown = Description(L"🏠👇");
static const Description kReachQuery = Description(L"🔮");
static const Description kDescriptionShell = Description(L"🌀");

void AppendStatus(const CommandReach& reach, LineBuilder& output) {
  SerializeCall(
      L"🦀", {StructureToString(reach.structure), reach.repetitions.ToString()},
      output);
}

void AppendStatus(const CommandReachBegin& reach, LineBuilder& output) {
  SerializeCall(
      (reach.direction == Direction::kBackwards
           ? (reach.structure == Structure::kLine ? kHomeUp : kHomeLeft)
           : (reach.structure == Structure::kLine ? kHomeDown : kHomeRight))
          .read(),
      {StructureToString(reach.structure), reach.repetitions.ToString()},
      output);
}

void AppendStatus(const CommandReachLine& reach_line, LineBuilder& output) {
  SerializeCall(
      reach_line.repetitions.get() >= 0 ? kMoveDown.read() : kMoveUp.read(),
      {reach_line.repetitions.ToString()}, output);
}

void AppendStatus(const CommandReachPage& reach_line, LineBuilder& output) {
  SerializeCall(
      reach_line.repetitions.get() >= 0 ? kPageDown.read() : kPageUp.read(),
      {reach_line.repetitions.ToString()}, output);
}

void AppendStatus(const CommandReachQuery& c, LineBuilder& output) {
  SerializeCall(
      kReachQuery.read(),
      {c.query + std::wstring(3 - std::min(3ul, c.query.size()), L'_')},
      output);
}

void AppendStatus(const CommandReachBisect& c, LineBuilder& output) {
  wchar_t backwards = c.structure == Structure::kLine ? L'👆' : L'👈';
  wchar_t forwards = c.structure == Structure::kLine ? L'👇' : L'👉';
  SerializeCall(L"🪓",
                {StructureToString(c.structure),
                 container::Materialize<std::wstring>(
                     c.directions |
                     std::views::transform([&](const Direction& direction) {
                       switch (direction) {
                         case Direction::kForwards:
                           return forwards;
                         case Direction::kBackwards:
                           return backwards;
                       }
                       LOG(FATAL) << "Invalid direction.";
                       return L' ';
                     }))},
                output);
}

void AppendStatus(const CommandSetShell& c, LineBuilder& output) {
  SerializeCall(kDescriptionShell.read(), {c.input}, output);
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
                                          Direction::kForwards),
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
                    CommandArgumentRepetitions(1), Structure::kLine,
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
                ApplyRepetitions(repetitions, Structure::kLine,
                                 NewMoveTransformation(operation_scope)));
          }}}));
}

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
  return ApplyRepetitions(reach_line.repetitions, Structure::kLine,
                          NewMoveTransformation(operation_scope));
}

transformation::Stack GetTransformation(
    const NonNull<std::shared_ptr<OperationScope>>& operation_scope,
    transformation::Stack&, CommandReachPage reach_page) {
  return ApplyRepetitions(reach_page.repetitions, Structure::kPage,
                          NewMoveTransformation(operation_scope));
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

  void AppendStatusString(LineBuilder& output) const {
    for (const auto& op : commands_) {
      output.AppendString(L" ", std::nullopt);
      std::visit([&output](auto& t) { AppendStatus(t, output); }, op);
    }
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
    const std::shared_ptr<InputReceiver> keyboard_redirect =
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

  futures::Value<gc::Root<OpenBuffer>> GetHelpBuffer() {
    return VisitOptional(
        [](gc::Root<OpenBuffer> buffer) {
          buffer.ptr()->Reload();
          return futures::Past(std::move(buffer));
        },
        [this] {
          return OpenAnonymousBuffer(editor_state_)
              .Transform([storage = help_buffer_](gc::Root<OpenBuffer> buffer) {
                buffer.ptr()->Set(buffer_variables::paste_mode, true);
                storage.value() = buffer;
                return futures::Past(buffer);
              });
        },
        help_buffer_.value());
  }

 private:
  futures::Value<EmptyValue> Update(ApplicationType application_type) {
    static Tracker tracker(L"State::Update");
    auto call = tracker.Call();
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
                      consumer = std::move(output.consumer),
                      transformation] mutable {
      return ExecuteTransformation(editor_state, application_type,
                                   transformation)
          .Transform([consumer = std::move(consumer)](
                         UndoCallback undo_callback) mutable {
            std::move(consumer)(std::move(undo_callback));
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
      {L'z', Structure::kChar},      {L'x', Structure::kWord},
      {L'c', Structure::kSymbol},    {L'v', Structure::kLine},
      {L'b', Structure::kParagraph}, {L'n', Structure::kPage},
      {L'm', Structure::kBuffer},    {L'C', Structure::kCursor},
      {L'V', Structure::kTree}};
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
    std::stringstream structure_stream;
    structure_stream << entry.second;
    std::wstring structure_name = FromByteString(structure_stream.str());
    cmap.Insert(entry.first,
                {.category = KeyCommandsMap::Category::kStructure,
                 .description = Description(structure_name),
                 .active = *structure == std::nullopt,
                 .handler =
                     [structure, repetitions, &entry](ExtendedChar) {
                       LOG(INFO) << "Running, storing: " << entry.second;
                       *structure = entry.second;
                       if (repetitions->get() == 0) {
                         repetitions->sum(1);
                       }
                     }})
        .Insert(entry.first, {.category = KeyCommandsMap::Category::kStructure,
                              .description = Description(structure_name),
                              .active = entry.second == *structure,
                              .handler = [repetitions](ExtendedChar) {
                                repetitions->sum(1);
                              }});
  };
}

void CheckIncrementsChar(KeyCommandsMap& cmap,
                         CommandArgumentRepetitions* output) {
  cmap.Insert(L'h', {.category = KeyCommandsMap::Category::kRepetitions,
                     .description = kMoveLeft,
                     .handler = [output](ExtendedChar) { output->sum(-1); }})
      .Insert(ControlChar::kLeftArrow,
              {.category = KeyCommandsMap::Category::kRepetitions,
               .description = kMoveLeft,
               .handler = [output](ExtendedChar) { output->sum(-1); }})
      .Insert(L'l', {.category = KeyCommandsMap::Category::kRepetitions,
                     .description = kMoveRight,
                     .handler = [output](ExtendedChar) { output->sum(1); }})
      .Insert(ControlChar::kRightArrow,
              {.category = KeyCommandsMap::Category::kRepetitions,
               .description = kMoveRight,
               .handler = [output](ExtendedChar) { output->sum(1); }});
}

void CheckRepetitionsChar(KeyCommandsMap& cmap,
                          CommandArgumentRepetitions* output) {
  cmap.Insert(ControlChar::kBackspace,
              {.category = KeyCommandsMap::Category::kStringControl,
               .description = Description(L"PopRepetitions"),
               .active = !output->empty(),
               .handler = [output](ExtendedChar) { output->PopValue(); }});
  for (int i = 0; i < 10; i++)
    cmap.Insert(L'0' + i,
                {.category = KeyCommandsMap::Category::kRepetitions,
                 .description = Description(L"Repetitions"),
                 .handler = [output, i](ExtendedChar) { output->factor(i); }});
}

static const Description kBisectLeft = Description(L"🪓👈");
static const Description kBisectRight = Description(L"🪓👉");
static const Description kBisectUp = Description(L"🪓👆");
static const Description kBisectDown = Description(L"🪓👇");

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReach* output,
                       State* state) {
  if (output->structure.value_or(Structure::kChar) == Structure::kChar &&
      !output->repetitions.empty()) {
    cmap.Insert(L'H', {.category = KeyCommandsMap::Category::kNewCommand,
                       .description = kBisectLeft,
                       .active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](ExtendedChar) {
                             state->Push(CommandReachBisect{
                                 .structure = Structure::kChar,
                                 .directions = {Direction::kBackwards}});
                           }})
        .Insert(L'L', {.category = KeyCommandsMap::Category::kNewCommand,
                       .description = kBisectRight,
                       .active = output->repetitions.get_list().back() > 0,
                       .handler = [state](ExtendedChar) {
                         state->Push(CommandReachBisect{
                             .structure = Structure::kChar,
                             .directions = {Direction::kForwards}});
                       }});
  }

  if (output->structure == Structure::kLine && !output->repetitions.empty()) {
    cmap.Insert(L'K', {.category = KeyCommandsMap::Category::kNewCommand,
                       .description = kBisectUp,
                       .active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](ExtendedChar) {
                             state->Push(CommandReachBisect{
                                 .structure = Structure::kLine,
                                 .directions = {Direction::kBackwards}});
                           }})
        .Insert(L'J', {.category = KeyCommandsMap::Category::kNewCommand,
                       .description = kBisectDown,
                       .active = output->repetitions.get_list().back() > 0,
                       .handler = [state](ExtendedChar) {
                         state->Push(CommandReachBisect{
                             .structure = Structure::kLine,
                             .directions = {Direction::kForwards}});
                       }});
  }

  CheckStructureChar(cmap, &output->structure, &output->repetitions);
  CheckIncrementsChar(cmap, &output->repetitions);
  CheckRepetitionsChar(cmap, &output->repetitions);
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachBegin* output,
                       State*) {
  if (output->structure == Structure::kLine) {
    auto handler = [&](Description description) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::kRepetitions,
          .description = description,
          .handler = [output](ExtendedChar t) {
            int delta = (t == ExtendedChar(L'j') ||
                         t == ExtendedChar(ControlChar::kDownArrow))
                            ? 1
                            : -1;
            if (output->direction == Direction::kBackwards) {
              delta *= -1;
            }
            output->repetitions.sum(delta);
          }};
    };
    cmap.Insert(L'j', handler(Description(L"👇")))
        .Insert(ControlChar::kDownArrow, handler(Description(L"👇")))
        .Insert(L'k', handler(Description(L"👆")))
        .Insert(ControlChar::kUpArrow, handler(Description(L"👆")));
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
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachLine* output,
                       State* state) {
  cmap.Insert(L'K', {.category = KeyCommandsMap::Category::kNewCommand,
                     .description = Description(L"🪓👆"),
                     .active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() < 0,
                     .handler =
                         [state](ExtendedChar) {
                           state->Push(CommandReachBisect{
                               .structure = Structure::kLine,
                               .directions = {Direction::kBackwards}});
                         }})
      .Insert(L'J', {.category = KeyCommandsMap::Category::kNewCommand,
                     .description = Description(L"🪓👇"),
                     .active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() > 0,
                     .handler = [state](ExtendedChar) {
                       state->Push(CommandReachBisect{
                           .structure = Structure::kLine,
                           .directions = {Direction::kForwards}});
                     }});

  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(
          L'j',
          {.category = KeyCommandsMap::Category::kRepetitions,
           .description = kMoveDown,
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(
          ControlChar::kDownArrow,
          {.category = KeyCommandsMap::Category::kRepetitions,
           .description = kMoveDown,
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(
          L'k',
          {.category = KeyCommandsMap::Category::kRepetitions,
           .description = kMoveUp,
           .handler = [output](ExtendedChar) { output->repetitions.sum(-1); }})
      .Insert(
          ControlChar::kUpArrow,
          {.category = KeyCommandsMap::Category::kRepetitions,
           .description = kMoveUp,
           .handler = [output](ExtendedChar) { output->repetitions.sum(-1); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachPage* output, State*) {
  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(
          ControlChar::kPageDown,
          {.category = KeyCommandsMap::Category::kNewCommand,
           .description = kPageDown,
           .handler = [output](ExtendedChar) { output->repetitions.sum(1); }})
      .Insert(
          ControlChar::kPageUp,
          {.category = KeyCommandsMap::Category::kNewCommand,
           .description = kPageUp,
           .handler = [output](ExtendedChar) { output->repetitions.sum(-1); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachQuery* output,
                       State*) {
  if (output->query.size() < 3)
    cmap.SetFallback(
        {L'\n', ControlChar::kEscape, ControlChar::kBackspace},
        [output](ExtendedChar extended_c) {
          std::visit(overload{[](ControlChar) {},
                              [&](wchar_t c) { output->query.push_back(c); }},
                     extended_c);
        });
  cmap.Insert(
      ControlChar::kBackspace,
      {.category = KeyCommandsMap::Category::kStringControl,
       .description = Description(L"Backspace"),
       .active = !output->query.empty(),
       .handler = [output](ExtendedChar) { output->query.pop_back(); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachBisect* output,
                       State*) {
  cmap.Insert(ControlChar::kBackspace,
              {.category = KeyCommandsMap::Category::kStringControl,
               .description = Description(L"Pop"),
               .active = !output->directions.empty(),
               .handler = [output](ExtendedChar) {
                 return output->directions.pop_back();
               }});

  if (output->structure.value_or(Structure::kChar) == Structure::kChar) {
    cmap.Insert(L'h',
                {.category = KeyCommandsMap::Category::kDirection,
                 .description = kBisectLeft,
                 .handler =
                     [output](ExtendedChar) {
                       output->directions.push_back(Direction::kBackwards);
                     }})
        .Insert(L'l', {.category = KeyCommandsMap::Category::kDirection,
                       .description = kBisectRight,
                       .handler = [output](ExtendedChar) {
                         output->directions.push_back(Direction::kForwards);
                       }});
  }
  if (output->structure == Structure::kLine) {
    cmap.Insert(L'k',
                {.category = KeyCommandsMap::Category::kDirection,
                 .description = kBisectDown,
                 .handler =
                     [output](ExtendedChar) {
                       output->directions.push_back(Direction::kBackwards);
                     }})
        .Insert(L'j', {.category = KeyCommandsMap::Category::kDirection,
                       .description = kBisectUp,
                       .handler = [output](ExtendedChar) {
                         output->directions.push_back(Direction::kForwards);
                       }});
  }
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandSetShell* output, State*) {
  cmap.Insert(ControlChar::kBackspace,
              {.category = KeyCommandsMap::Category::kStringControl,
               .description = Description(L"Backspace"),
               .active = !output->input.empty(),
               .handler = [output](ExtendedChar) { output->input.pop_back(); }})
      .SetFallback(
          {'\n', ControlChar::kEscape, ControlChar::kBackspace},
          [output](ExtendedChar extended_c) {
            std::visit(overload{[](ControlChar) {},
                                [&](wchar_t c) { output->input.push_back(c); }},
                       extended_c);
          });
}

class OperationMode : public EditorMode {
 public:
  OperationMode(TopCommand top_command, EditorState& editor_state)
      : editor_state_(editor_state),
        state_(editor_state, std::move(top_command)) {}

  void ProcessInput(ExtendedChar c) override {
    editor_state_.status().Reset();
    GetGlobalKeyCommandsMap().Execute(c);
  }

  CursorMode cursor_mode() const override { return CursorMode::kDefault; }

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const override {
    return {};
  }

  void ShowStatus() {
    LineBuilder output;
    AppendStatus(state_.top_command(), output);
    output.AppendString(L":", LineModifierSet{LineModifier::kDim});
    state_.AppendStatusString(output);
    AppendStatusForCommandsAvailable(output);
    editor_state_.status().SetInformationText(
        MakeNonNullShared<Line>(std::move(output).Build()));
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
        .Insert(L'\n', {.category = KeyCommandsMap::Category::kTop,
                        .description = Description(L"Apply"),
                        .handler = [this](ExtendedChar) { state_.Commit(); }})
        .Insert(ControlChar::kBackspace,
                {.category = KeyCommandsMap::Category::kStringControl,
                 .description = Description(L"Backspace"),
                 .handler = [this](ExtendedChar) {
                   state_.UndoLast();
                   ShowStatus();
                 }});

    KeyCommandsMap& structure_keys = cmap.PushNew();
    for (const std::pair<const wchar_t, Structure>& entry :
         structure_bindings())
      structure_keys.Insert(
          entry.first,
          {.category = KeyCommandsMap::Category::kStructure,
           .description = Description(StructureToString(entry.second)),
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
                {.category = KeyCommandsMap::Category::kNewCommand,
                 .description = kMoveLeft,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::kChar,
                                                .repetitions = -1});
                     }})
        .Insert(ControlChar::kLeftArrow,
                {.category = KeyCommandsMap::Category::kNewCommand,
                 .description = kMoveLeft,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::kChar,
                                                .repetitions = -1});
                     }})
        .Insert(L'l',
                {.category = KeyCommandsMap::Category::kNewCommand,
                 .description = kMoveRight,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::kChar,
                                                .repetitions = 1});
                     }})
        .Insert(ControlChar::kRightArrow,
                {.category = KeyCommandsMap::Category::kNewCommand,
                 .description = kMoveRight,
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(CommandReach{.structure = Structure::kChar,
                                                .repetitions = 1});
                     }})
        .OnHandle([this] {
          state_.Update();
          ShowStatus();
        });

    cmap.PushBack(ReceiveInputTopCommand(state_.top_command()));

    // Unhandled character.
    cmap.PushNew()
        .Insert(ControlChar::kEscape,
                {.category = KeyCommandsMap::Category::kStringControl,
                 .description = Description(L"Cancel"),
                 .handler =
                     [&state = state_](ExtendedChar) {
                       if (state.top_command().post_transformation_behavior ==
                           transformation::Stack::kNone) {
                         state.Abort();
                       } else {
                         TopCommand top_command = state.top_command();
                         top_command.post_transformation_behavior =
                             transformation::Stack::kNone;
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
    output.AppendString(L"    ", std::nullopt);
    output.Append(LineBuilder(GetGlobalKeyCommandsMap().SummaryLine()));
  }

  KeyCommandsMap ReceiveInputTopCommand(TopCommand top_command) {
    using PTB = transformation::Stack::PostTransformationBehavior;
    auto push = [&state = state_](Description description, Command value) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::kNewCommand,
          .description = description,
          .handler = [&state, value](ExtendedChar) { state.Push(value); }};
    };
    auto PageHandler = [&](ControlChar c) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::kNewCommand,
          .description = c == ControlChar::kPageUp ? kPageUp : kPageDown,
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
                    c == ControlChar::kPageUp ? -1 : 1)});
          }};
    };
    auto MoveHandler = [&](ExtendedChar c) {
      CHECK(c == ExtendedChar(L'j') || c == ExtendedChar(L'k') ||
            c == ExtendedChar(ControlChar::kDownArrow) ||
            c == ExtendedChar(ControlChar::kUpArrow));
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::kNewCommand,
          .description = (c == ExtendedChar(L'j') ||
                          c == ExtendedChar(ControlChar::kDownArrow))
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
                            c == ExtendedChar(ControlChar::kUpArrow)
                        ? -1
                        : 1)});
          }};
    };
    KeyCommandsMap cmap;
    cmap.OnHandle([this] { ShowStatus(); });
    cmap.Insert(L'd',
                {.category = KeyCommandsMap::Category::kTop,
                 .description = Description(L"Delete"),
                 .handler =
                     [top_command, &state = state_](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::kDeleteRegion:
                           top_command.post_transformation_behavior =
                               PTB::kCopyRegion;
                           break;
                         case PTB::kCopyRegion:
                           top_command.post_transformation_behavior =
                               PTB::kNone;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::kDeleteRegion;
                           break;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'?',
                {.category = KeyCommandsMap::Category::kTop,
                 .description = Description(L"Help"),
                 .handler =
                     [&state = state_, top_command](ExtendedChar) mutable {
                       top_command.show_help = !top_command.show_help;
                       state.set_top_command(top_command);
                     }})
        .Insert(L'~',
                {.category = KeyCommandsMap::Category::kTop,
                 .description = Description(L"SwitchCase"),
                 .handler =
                     [top_command, &state = state_](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::kCapitalsSwitch:
                           top_command.post_transformation_behavior =
                               PTB::kNone;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::kCapitalsSwitch;
                           break;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'$',
                {.category = KeyCommandsMap::Category::kTop,
                 .description = Description(L"Shell"),
                 .handler =
                     [top_command, &state = state_](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::kCommandSystem:
                           top_command.post_transformation_behavior =
                               PTB::kCommandCpp;
                           break;
                         case PTB::kCommandCpp:
                           top_command.post_transformation_behavior =
                               PTB::kNone;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::kCommandSystem;
                           break;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'|', push(kDescriptionShell, CommandSetShell{}))
        .Insert(L'+',
                {.category = KeyCommandsMap::Category::kTop,
                 .description = Description(L"CursorEveryLine"),
                 .handler =
                     [&state = state_, top_command](ExtendedChar) mutable {
                       switch (top_command.post_transformation_behavior) {
                         case PTB::kCursorOnEachLine:
                           top_command.post_transformation_behavior =
                               PTB::kNone;
                           break;
                         default:
                           top_command.post_transformation_behavior =
                               PTB::kCursorOnEachLine;
                       }
                       state.set_top_command(top_command);
                     }})
        .Insert(L'f', push(kReachQuery, CommandReachQuery{}))
        .Insert(ControlChar::kPageDown, PageHandler(ControlChar::kPageDown))
        .Insert(ControlChar::kPageUp, PageHandler(ControlChar::kPageUp))
        .Insert(L'j', MoveHandler('j'))
        .Insert(L'k', MoveHandler('k'))
        .Insert(ControlChar::kDownArrow, MoveHandler(ControlChar::kDownArrow))
        .Insert(ControlChar::kUpArrow, MoveHandler(ControlChar::kUpArrow))
        .Insert(L'H', push(kHomeLeft, CommandReachBegin{}))
        .Insert(L'L',
                push(kHomeRight,
                     CommandReachBegin{.direction = Direction::kBackwards}))
        .Insert(L'K',
                push(kHomeUp, CommandReachBegin{.structure = Structure::kLine}))
        .Insert(L'J', push(kHomeDown, CommandReachBegin{
                                          .structure = Structure::kLine,
                                          .direction = Direction::kBackwards}));
    return cmap;
  }

  static void AppendStatus(TopCommand top_command, LineBuilder& output) {
    switch (top_command.post_transformation_behavior) {
      case transformation::Stack::PostTransformationBehavior::kNone:
        output.AppendString(L"🦋 Move", LineModifierSet{LineModifier::kBold,
                                                        LineModifier::kCyan});
        return;
      case transformation::Stack::PostTransformationBehavior::kDeleteRegion:
        output.AppendString(
            L"✂️  Delete",
            LineModifierSet{LineModifier::kBold, LineModifier::kBgRed});
        return;
      case transformation::Stack::PostTransformationBehavior::kCopyRegion:
        output.AppendString(L"📋 Copy", LineModifierSet{LineModifier::kBold,
                                                        LineModifier::kYellow});
        return;
      case transformation::Stack::PostTransformationBehavior::kCommandSystem:
        output.AppendString(
            L"🐚 System",
            LineModifierSet{LineModifier::kBold, LineModifier::kGreen});
        return;
      case transformation::Stack::PostTransformationBehavior::kCommandCpp:
        output.AppendString(
            L"🤖 Cpp",
            LineModifierSet{LineModifier::kBold, LineModifier::kGreen,
                            LineModifier::kUnderline});
        return;
      case transformation::Stack::PostTransformationBehavior::kCapitalsSwitch:
        output.AppendString(L"🔠 Aa", LineModifierSet{LineModifier::kBold,
                                                      LineModifier::kMagenta});
        return;
      case transformation::Stack::PostTransformationBehavior::kCursorOnEachLine:
        output.AppendString(
            L"Ꮖ Cursor",
            LineModifierSet{LineModifier::kBold, LineModifier::kMagenta});
        return;
    }
    LOG(FATAL) << "Invalid post transformation behavior.";
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
  return container::Sum(get_list());
}

std::list<int> CommandArgumentRepetitions::get_list() const {
  return container::Materialize<std::list<int>>(
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
                                                  std::wstring description,
                                                  TopCommand top_command,
                                                  EditorState& editor_state,
                                                  Command command) {
  return NewSetModeCommand({.editor_state = editor_state,
                            .description = description,
                            .category = L"Edit",
                            .factory = [top_command, &editor_state, command] {
                              auto output = std::make_unique<OperationMode>(
                                  top_command, editor_state);
                              output->PushCommand(command);
                              output->ShowStatus();
                              return output;
                            }});
}

}  // namespace afc::editor::operation
