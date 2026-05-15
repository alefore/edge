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
#include "src/operation_boundary.h"
#include "src/operation_find_local.h"
#include "src/operation_move.h"
#include "src/operation_move_line.h"
#include "src/operation_move_page.h"
#include "src/operation_paste.h"
#include "src/operation_scope.h"
#include "src/operation_set_shell.h"
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
      output.Append(op->status());
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
    if (commands_.empty()) Push(commands::Move(Structure::Char, 1));
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
      stack.push_back(command->GetTransformation(operation_scope_, stack));
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

const std::unordered_map<wchar_t, Structure>& structure_bindings() {
  static const std::unordered_map<wchar_t, Structure> output = {
      {L'z', Structure::Char},      {L'x', Structure::Word},
      {L'c', Structure::Symbol},    {L'v', Structure::Line},
      {L'b', Structure::Paragraph}, {L'n', Structure::Page},
      {L'm', Structure::Buffer},    {L'C', Structure::Cursor},
      {L'V', Structure::Tree}};
  return output;
}

}  // namespace
namespace commands {
void AddSetStructureChar(KeyCommandsMap& cmap,
                         ValueWithDefault<Structure>& structure,
                         Repetitions& repetitions) {
  std::ranges::for_each(
      structure_bindings(),
      [&](const std::pair<const wchar_t, Structure>& entry) {
        VLOG(9) << "Add key: " << entry.second;
        NonEmptySingleLine structure_name =
            commands::StructureToString(entry.second);
        cmap.Insert(
            entry.first,
            {.category = KeyCommandsMap::Category::Structure,
             .description = Description{structure_name},
             .active = structure.value() == entry.second,
             .handler = [&structure, &repetitions, &entry](ExtendedChar) {
               LOG(INFO) << "Running, storing: " << entry.second;
               if (!structure.IsExplicit()) {
                 if (structure.value() == entry.second ||
                     repetitions.get() == 0)
                   repetitions.sum(1);
                 structure.Set(entry.second);
               } else
                 repetitions.sum(1);
             }});
      });
}
}  // namespace commands
namespace {

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
      cmap.PushBack(std::move(
          state_.GetLastCommand()
              ->key_commands_map(
                  [&state = state_](
                      NonNull<std::shared_ptr<MoveOperationCommand>> command) {
                    state.Push(std::move(command));
                  })
              .OnHandle([this] {
                if (state_.empty())
                  PushCommand(commands::Move(Structure::Char, 1));
                state_.Update();
                ShowStatus();
              })));
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
               if (const std::optional<commands::Repetitions*> repetitions =
                       state_.GetLastCommand()->repetitions();
                   repetitions.has_value() && !(*repetitions)->empty()) {
                 last_repetitions = (*repetitions)->get_list().back();
               }
             }
             state_.Push(commands::Move(
                 structure,
                 last_repetitions < 0 ? -1 : (last_repetitions > 0 ? 1 : 0)));
           }});

    structure_keys
        .Insert({L'h', ControlChar::LeftArrow},
                {.category = KeyCommandsMap::Category::NewCommand,
                 .description = commands::MoveLeftDescription(),
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(commands::Move(Structure::Char, -1));
                     }})
        .Insert({L'l', ControlChar::RightArrow},
                {.category = KeyCommandsMap::Category::NewCommand,
                 .description = commands::MoveRightDescription(),
                 .handler =
                     [this](ExtendedChar) {
                       state_.Push(commands::Move(Structure::Char, 1));
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
          .description = c == ControlChar::PageUp
                             ? commands::MovePageUpDescription()
                             : commands::MovePageDownDescription(),
          .handler = [&state = state_, c](ExtendedChar) {
#if 0
            if (Move* reach =
                    state.empty()
                        ? nullptr
                        : std::get_if<Move>(&state.GetLastCommand());
                reach != nullptr && reach->structure == std::nullopt) {
              state.UndoLast();
            }
#endif
            state.Push(commands::MovePage(operation::commands::Repetitions(
                c == ControlChar::PageUp ? -1 : 1)));
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
                             ? commands::MoveDownDescription()
                             : commands::MoveUpDescription(),
          .handler = [&state = state_, c](ExtendedChar) {
#if 0
            if (Move* reach =
                    state.empty()
                        ? nullptr
                        : std::get_if<Move>(&state.GetLastCommand());
                reach != nullptr && reach->structure == std::nullopt &&
                reach->repetitions.empty()) {
              state.UndoLast();
            }
#endif
            state.Push(commands::MoveLine(operation::commands::Repetitions(
                c == ExtendedChar(L'k') ||
                        c == ExtendedChar(ControlChar::UpArrow)
                    ? -1
                    : 1)));
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
        .Insert(L'p',
                KeyCommandsMap::KeyCommand{
                    .category = KeyCommandsMap::Category::NewCommand,
                    .description = commands::PasteDescription(),
                    .active = editor_state_.buffer_registry()
                                  .Find(PasteBuffer{})
                                  .has_value(),
                    .handler =
                        [&state = state_](ExtendedChar) {
                          state.Push(commands::Paste());
                        }})
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
        .Insert(L'|',
                push(commands::SetShellDescription(), commands::SetShell()))
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
        .Insert({L'H', ControlChar::Home},
                push(commands::HomeLeftDescription(),
                     commands::Boundary(commands::BoundaryOptions{})))
        .Insert({L'L', ControlChar::End},
                push(commands::HomeRightDescription(),
                     commands::Boundary(commands::BoundaryOptions{
                         .direction = Direction::Backwards})))
        .Insert(L'K', push(commands::HomeUpDescription(),
                           commands::Boundary(commands::BoundaryOptions{
                               .structure = Structure::Line})))
        .Insert(L'J', push(commands::HomeDownDescription(),
                           commands::Boundary(commands::BoundaryOptions{
                               .structure = Structure::Line,
                               .direction = Direction::Backwards})));
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
    LOG(FATAL) << "Invalid post transformation behavior.";
  }

  EditorState& editor_state_;
  State state_;
};
}  // namespace

gc::Root<afc::editor::Command> NewTopLevelCommand(
    std::wstring, LazyString description, TopCommand top_command,
    EditorState& editor_state, std::function<Command()> command) {
  return NewSetModeCommand(
      {.editor_state = editor_state,
       .description = description,
       .category = CommandCategory::kEdit(),
       .factory = [top_command, &editor_state, command] {
         auto output =
             MakeNonNullUnique<OperationMode>(top_command, editor_state);
         output->PushCommand(command());
         output->ShowStatus();
         return editor_state.gc_pool().NewRoot<InputReceiver>(
             std::move(output));
       }});
}

}  // namespace afc::editor::operation
