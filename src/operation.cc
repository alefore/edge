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
#include "src/language/overload.h"
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
using infrastructure::screen::LineModifier;
using infrastructure::screen::LineModifierSet;
using infrastructure::screen::VisualOverlayMap;
using language::EmptyValue;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::lazy_string::Append;
using language::lazy_string::LazyString;
using language::lazy_string::NewLazyString;
using language::text::Line;
using language::text::LineBuilder;

namespace gc = language::gc;

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

void AppendStatus(const CommandReach& reach, LineBuilder& output) {
  SerializeCall(
      L"🦀", {StructureToString(reach.structure), reach.repetitions.ToString()},
      output);
}

void AppendStatus(const CommandReachBegin& reach, LineBuilder& output) {
  SerializeCall(
      reach.direction == Direction::kBackwards ? L"🏠👇" : L"🏠👆",
      {StructureToString(reach.structure), reach.repetitions.ToString()},
      output);
}

void AppendStatus(const CommandReachLine& reach_line, LineBuilder& output) {
  SerializeCall(reach_line.repetitions.get() >= 0 ? L"🧗👇" : L"🧗👆",
                {reach_line.repetitions.ToString()}, output);
}

void AppendStatus(const CommandReachPage& reach_line, LineBuilder& output) {
  SerializeCall(reach_line.repetitions.get() >= 0 ? L"📜👇" : L"📜👆",
                {reach_line.repetitions.ToString()}, output);
}

void AppendStatus(const CommandReachQuery& c, LineBuilder& output) {
  SerializeCall(
      L"🔮", {c.query + std::wstring(3 - std::min(3ul, c.query.size()), L'_')},
      output);
}

void AppendStatus(const CommandReachBisect& c, LineBuilder& output) {
  std::wstring directions;
  wchar_t backwards = c.structure == Structure::kLine ? L'👆' : L'👈';
  wchar_t forwards = c.structure == Structure::kLine ? L'👇' : L'👉';
  for (Direction direction : c.directions) switch (direction) {
      case Direction::kForwards:
        directions.push_back(forwards);
        break;
      case Direction::kBackwards:
        directions.push_back(backwards);
        break;
    }
  SerializeCall(L"🪓", {StructureToString(c.structure), directions}, output);
}

void AppendStatus(const CommandSetShell& c, LineBuilder& output) {
  SerializeCall(L"🌀", {c.input}, output);
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
  enum class Category {
    kStringControl,
    kRepetitions,
    kDirection,
    kStructure,
    kNewCommand,
    kTop,
  };

  struct KeyCommand {
    Category category;
    std::wstring description = L"";
    bool active = true;
    std::function<void(wchar_t)> handler;
  };

  KeyCommandsMap() = default;
  KeyCommandsMap(const KeyCommandsMap&) = delete;
  KeyCommandsMap(KeyCommandsMap&&) = default;

  KeyCommandsMap& Insert(wchar_t c, KeyCommand command) {
    if (command.active) table_.insert({c, std::move(command)});
    return *this;
  }

  KeyCommandsMap& Insert(std::set<wchar_t> chars, KeyCommand command) {
    if (command.active)
      for (wchar_t c : chars) table_.insert({c, command});
    return *this;
  }

  template <typename Value, typename Callable>
  KeyCommandsMap& Insert(const std::unordered_map<wchar_t, Value>& values,
                         Category category, Callable callback) {
    for (const auto& entry : values)
      Insert(entry.first,
             {.category = category, .handler = [callback, entry](wchar_t) {
                callback(entry.second);
              }});
    return *this;
  }

  KeyCommandsMap& Erase(wchar_t c) {
    table_.erase(c);
    return *this;
  }

  KeyCommandsMap& SetFallback(std::set<wchar_t> exclude,
                              std::function<void(wchar_t)> callback) {
    CHECK(fallback_ == nullptr);
    CHECK(callback != nullptr);
    fallback_exclusion_ = std::move(exclude);
    fallback_ = std::move(callback);
    return *this;
  }

  KeyCommandsMap& OnHandle(std::function<void()> handler) {
    CHECK(on_handle_ == nullptr);
    on_handle_ = handler;
    return *this;
  }

  std::function<void(wchar_t)> FindCallbackOrNull(wchar_t c) const {
    if (auto it = table_.find(c); it != table_.end()) return it->second.handler;
    if (HasFallback() &&
        fallback_exclusion_.find(c) == fallback_exclusion_.end())
      return fallback_;
    return nullptr;
  }

  bool HasFallback() const { return fallback_ != nullptr; }

  bool Execute(wchar_t c) const {
    if (auto callback = FindCallbackOrNull(c); callback != nullptr) {
      callback(c);
      if (on_handle_ != nullptr) on_handle_();
      return true;
    }
    return false;
  }

  void ExtractKeys(std::map<wchar_t, Category>& output) const {
    for (auto& entry : table_)
      output.insert({entry.first, entry.second.category});
  }

 private:
  std::unordered_map<wchar_t, KeyCommand> table_;
  std::set<wchar_t> fallback_exclusion_;
  std::function<void(wchar_t)> fallback_ = nullptr;
  std::function<void()> on_handle_ = nullptr;
};

class KeyCommandsMapSequence {
 public:
  bool Execute(wchar_t c) const {
    for (const auto& cmap : sequence_) {
      if (cmap.Execute(c)) return true;
    }
    return false;
  }

  KeyCommandsMapSequence& PushBack(KeyCommandsMap cmap) {
    sequence_.push_back(std::move(cmap));
    return *this;
  }

  KeyCommandsMap& PushNew() {
    PushBack(KeyCommandsMap());
    return sequence_.back();
  }

  std::map<wchar_t, KeyCommandsMap::Category> GetKeys() {
    std::map<wchar_t, KeyCommandsMap::Category> output;
    for (const KeyCommandsMap& entry : sequence_) {
      entry.ExtractKeys(output);
      if (entry.HasFallback()) break;
    }
    return output;
  }

 private:
  std::vector<KeyCommandsMap> sequence_;
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

  for (const auto& entry : structure_bindings()) {
    VLOG(9) << "Add key: " << entry.second;
    cmap.Insert(entry.first, {.category = KeyCommandsMap::Category::kStructure,
                              .active = *structure == std::nullopt,
                              .handler =
                                  [structure, repetitions, &entry](wchar_t) {
                                    LOG(INFO)
                                        << "Running, storing: " << entry.second;
                                    *structure = entry.second;
                                    if (repetitions->get() == 0) {
                                      repetitions->sum(1);
                                    }
                                  }})
        .Insert(entry.first,
                {.category = KeyCommandsMap::Category::kStructure,
                 .active = entry.second == *structure,
                 .handler = [repetitions](wchar_t) { repetitions->sum(1); }});
  };
}

void CheckIncrementsChar(KeyCommandsMap& cmap,
                         CommandArgumentRepetitions* output) {
  cmap.Insert(L'h', {.category = KeyCommandsMap::Category::kRepetitions,
                     .handler = [output](wchar_t) { output->sum(-1); }})
      .Insert(L'l', {.category = KeyCommandsMap::Category::kRepetitions,
                     .handler = [output](wchar_t) { output->sum(1); }});
}

void CheckRepetitionsChar(KeyCommandsMap& cmap,
                          CommandArgumentRepetitions* output) {
  cmap.Insert(Terminal::BACKSPACE,
              {.category = KeyCommandsMap::Category::kStringControl,
               .active = !output->empty(),
               .handler = [output](wchar_t) { output->PopValue(); }});
  for (int i = 0; i < 10; i++)
    cmap.Insert(L'0' + i,
                {.category = KeyCommandsMap::Category::kRepetitions,
                 .handler = [output, i](wchar_t) { output->factor(i); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReach* output,
                       State* state) {
  if (output->structure.value_or(Structure::kChar) == Structure::kChar &&
      !output->repetitions.empty()) {
    cmap.Insert(L'H', {.category = KeyCommandsMap::Category::kNewCommand,
                       .active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](wchar_t) {
                             state->Push(CommandReachBisect{
                                 .structure = Structure::kChar,
                                 .directions = {Direction::kBackwards}});
                           }})
        .Insert(L'L', {.category = KeyCommandsMap::Category::kNewCommand,
                       .active = output->repetitions.get_list().back() > 0,
                       .handler = [state](wchar_t) {
                         state->Push(CommandReachBisect{
                             .structure = Structure::kChar,
                             .directions = {Direction::kForwards}});
                       }});
  }

  if (output->structure == Structure::kLine && !output->repetitions.empty()) {
    cmap.Insert(L'K', {.category = KeyCommandsMap::Category::kNewCommand,
                       .active = output->repetitions.get_list().back() < 0,
                       .handler =
                           [state](wchar_t) {
                             state->Push(CommandReachBisect{
                                 .structure = Structure::kLine,
                                 .directions = {Direction::kBackwards}});
                           }})
        .Insert(L'J', {.category = KeyCommandsMap::Category::kNewCommand,
                       .active = output->repetitions.get_list().back() > 0,
                       .handler = [state](wchar_t) {
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
    KeyCommandsMap::KeyCommand handler = {
        .category = KeyCommandsMap::Category::kRepetitions,
        .handler = [output](wchar_t t) {
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
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachLine* output,
                       State* state) {
  cmap.Insert(L'K', {.category = KeyCommandsMap::Category::kNewCommand,
                     .active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() < 0,
                     .handler =
                         [state](wchar_t) {
                           state->Push(CommandReachBisect{
                               .structure = Structure::kLine,
                               .directions = {Direction::kBackwards}});
                         }})
      .Insert(L'J', {.category = KeyCommandsMap::Category::kNewCommand,
                     .active = !output->repetitions.empty() &&
                               output->repetitions.get_list().back() > 0,
                     .handler = [state](wchar_t) {
                       state->Push(CommandReachBisect{
                           .structure = Structure::kLine,
                           .directions = {Direction::kForwards}});
                     }});

  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(L'j',
              {.category = KeyCommandsMap::Category::kRepetitions,
               .handler = [output](wchar_t) { output->repetitions.sum(1); }})
      .Insert(L'k',
              {.category = KeyCommandsMap::Category::kRepetitions,
               .handler = [output](wchar_t) { output->repetitions.sum(-1); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachPage* output, State*) {
  CheckRepetitionsChar(cmap, &output->repetitions);
  cmap.Insert(Terminal::PAGE_DOWN,
              {.category = KeyCommandsMap::Category::kNewCommand,
               .handler = [output](wchar_t) { output->repetitions.sum(1); }})
      .Insert(Terminal::PAGE_UP,
              {.category = KeyCommandsMap::Category::kNewCommand,
               .handler = [output](wchar_t) { output->repetitions.sum(-1); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachQuery* output,
                       State*) {
  if (output->query.size() < 3)
    cmap.SetFallback({L'\n', Terminal::ESCAPE, Terminal::BACKSPACE},
                     [output](wchar_t c) { output->query.push_back(c); });
  cmap.Insert(Terminal::BACKSPACE,
              {.category = KeyCommandsMap::Category::kStringControl,
               .active = !output->query.empty(),
               .handler = [output](wchar_t) { output->query.pop_back(); }});
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandReachBisect* output,
                       State*) {
  cmap.Insert(
      Terminal::BACKSPACE,
      {.category = KeyCommandsMap::Category::kStringControl,
       .active = !output->directions.empty(),
       .handler = [output](wchar_t) { return output->directions.pop_back(); }});

  if (output->structure.value_or(Structure::kChar) == Structure::kChar) {
    cmap.Insert(L'h',
                {.category = KeyCommandsMap::Category::kDirection,
                 .handler =
                     [output](wchar_t) {
                       output->directions.push_back(Direction::kBackwards);
                     }})
        .Insert(L'l', {.category = KeyCommandsMap::Category::kDirection,
                       .handler = [output](wchar_t) {
                         output->directions.push_back(Direction::kForwards);
                       }});
  }
  if (output->structure == Structure::kLine) {
    cmap.Insert(L'k',
                {.category = KeyCommandsMap::Category::kDirection,
                 .handler =
                     [output](wchar_t) {
                       output->directions.push_back(Direction::kBackwards);
                     }})
        .Insert(L'j', {.category = KeyCommandsMap::Category::kDirection,
                       .handler = [output](wchar_t) {
                         output->directions.push_back(Direction::kForwards);
                       }});
  }
}

void GetKeyCommandsMap(KeyCommandsMap& cmap, CommandSetShell* output, State*) {
  cmap.Insert(Terminal::BACKSPACE,
              {.category = KeyCommandsMap::Category::kStringControl,
               .active = !output->input.empty(),
               .handler = [output](wchar_t) { output->input.pop_back(); }})
      .SetFallback({'\n', Terminal::ESCAPE, Terminal::BACKSPACE},
                   [output](wchar_t c) { output->input.push_back(c); });
}

class OperationMode : public EditorMode {
 public:
  OperationMode(TopCommand top_command, EditorState& editor_state)
      : editor_state_(editor_state),
        state_(editor_state, std::move(top_command)) {}

  void ProcessInput(wint_t c) override {
    editor_state_.status().Reset();
    GetGlobalKeyCommandsMap().Execute(c);
  }

  CursorMode cursor_mode() const override { return CursorMode::kDefault; }

  void ShowStatus() {
    LineBuilder output;
    AppendStatus(state_.top_command(), output);
    output.AppendString(L":", LineModifierSet{LineModifier::kDim});
    state_.AppendStatusString(output);
    AppendStatusForCommandsAvailable(output);
    editor_state_.status().SetInformationText(
        MakeNonNullShared<Line>(std::move(output).Build()));
  }

  void PushDefault() { PushCommand(CommandReach()); }

  void PushCommand(Command command) { state_.Push(std::move(command)); }

 private:
  KeyCommandsMapSequence GetGlobalKeyCommandsMap() {
    KeyCommandsMapSequence cmap;

    if (!state_.empty()) {
      std::visit(
          [&](auto& t) {
            GetKeyCommandsMap(cmap.PushNew().OnHandle([this] {
              if (state_.empty()) PushDefault();
              state_.Update();
              ShowStatus();
            }),
                              &t, &state_);
          },
          state_.GetLastCommand());
    }

    cmap.PushNew()
        .Insert(L'\n', {.category = KeyCommandsMap::Category::kTop,
                        .handler = [this](wchar_t) { state_.Commit(); }})
        .Insert(Terminal::BACKSPACE,
                {.category = KeyCommandsMap::Category::kStringControl,
                 .handler = [this](wchar_t) {
                   state_.UndoLast();
                   ShowStatus();
                 }});

    cmap.PushNew()
        .Insert(structure_bindings(), KeyCommandsMap::Category::kStructure,
                [this](Structure structure) {
                  int last_repetitions = 0;
                  if (!state_.empty()) {
                    if (const std::optional<CommandArgumentRepetitions*>
                            repetitions =
                                GetRepetitions(state_.GetLastCommand());
                        repetitions.has_value() && !(*repetitions)->empty()) {
                      last_repetitions = (*repetitions)->get_list().back();
                    }
                  }
                  state_.Push(CommandReach{
                      .structure = structure,
                      .repetitions = last_repetitions < 0
                                         ? -1
                                         : (last_repetitions > 0 ? 1 : 0)});
                })
        .Insert(
            {L'h', L'l'},
            {.category = KeyCommandsMap::Category::kNewCommand,
             .handler =
                 [this](wchar_t c) {
                   state_.Push(CommandReach{.structure = Structure::kChar,
                                            .repetitions = c == L'h' ? -1 : 1});
                 }})
        .OnHandle([this] {
          state_.Update();
          ShowStatus();
        });

    cmap.PushBack(ReceiveInputTopCommand(state_.top_command()));

    // Unhandled character.
    cmap.PushNew()
        .Insert(Terminal::ESCAPE,
                {.category = KeyCommandsMap::Category::kStringControl,
                 .handler =
                     [&state = state_](wchar_t) {
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
        .SetFallback(
            {}, [&state = state_, &editor_state = editor_state_](wchar_t c) {
              state.Commit();
              editor_state.ProcessInput(c);
            });
    return cmap;
  }

  void AppendStatusForCommandsAvailable(LineBuilder& output) {
    KeyCommandsMapSequence cmap = GetGlobalKeyCommandsMap();
    output.AppendString(L"    ", std::nullopt);

    std::map<KeyCommandsMap::Category, std::wstring> entries_by_category;
    for (const std::pair<const wchar_t, KeyCommandsMap::Category>& entry :
         cmap.GetKeys())
      if (isprint(entry.first))
        entries_by_category[entry.second].push_back(entry.first);
    for (const std::pair<const KeyCommandsMap::Category, std::wstring>&
             category : entries_by_category) {
      output.AppendString(L" ", std::nullopt);
      output.AppendString(category.second, LineModifierSet{LineModifier::kDim});
    }
  }

  KeyCommandsMap ReceiveInputTopCommand(TopCommand top_command) {
    using PTB = transformation::Stack::PostTransformationBehavior;
    auto push = [&state = state_](Command value) {
      return KeyCommandsMap::KeyCommand{
          .category = KeyCommandsMap::Category::kNewCommand,
          .handler = [&state, value](wchar_t) { state.Push(value); }};
    };
    KeyCommandsMap cmap;
    cmap.OnHandle([this] { ShowStatus(); });
    cmap.Insert(L'd', {.category = KeyCommandsMap::Category::kTop,
                       .handler =
                           [top_command, &state = state_](wchar_t) mutable {
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
        .Insert(L'~', {.category = KeyCommandsMap::Category::kTop,
                       .handler =
                           [top_command, &state = state_](wchar_t) mutable {
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
        .Insert(L'$', {.category = KeyCommandsMap::Category::kTop,
                       .handler =
                           [top_command, &state = state_](wchar_t) mutable {
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
        .Insert(L'|', push(CommandSetShell{}))
        .Insert(L'+', {.category = KeyCommandsMap::Category::kTop,
                       .handler =
                           [&state = state_, top_command](wchar_t) mutable {
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
        .Insert(L'f', push(CommandReachQuery{}))
        .Insert(
            {Terminal::PAGE_DOWN, Terminal::PAGE_UP},
            {.category = KeyCommandsMap::Category::kNewCommand,
             .handler =
                 [&state = state_](wchar_t t) {
                   if (CommandReach* reach = state.empty()
                                                 ? nullptr
                                                 : std::get_if<CommandReach>(
                                                       &state.GetLastCommand());
                       reach != nullptr && reach->structure == std::nullopt) {
                     state.UndoLast();
                   }
                   state.Push(CommandReachPage{
                       .repetitions = operation::CommandArgumentRepetitions(
                           static_cast<int>(t) == Terminal::PAGE_UP ? -1 : 1)});
                 }})
        .Insert(
            {L'j', L'k'},
            {.category = KeyCommandsMap::Category::kNewCommand,
             .handler =
                 [&state = state_](wchar_t t) {
                   if (CommandReach* reach = state.empty()
                                                 ? nullptr
                                                 : std::get_if<CommandReach>(
                                                       &state.GetLastCommand());
                       reach != nullptr && reach->structure == std::nullopt) {
                     state.UndoLast();
                   }
                   state.Push(CommandReachLine{
                       .repetitions = operation::CommandArgumentRepetitions(
                           t == L'k' ? -1 : 1)});
                 }})
        .Insert(L'H', push(CommandReachBegin{}))
        .Insert(L'L',
                push(CommandReachBegin{.direction = Direction::kBackwards}))
        .Insert(L'K', push(CommandReachBegin{.structure = Structure::kLine}))
        .Insert(L'J',
                push(CommandReachBegin{.structure = Structure::kLine,
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
