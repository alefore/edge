#include "src/command_mode.h"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "src/buffer_contents_util.h"
#include "src/buffer_registry.h"
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/completion_model.h"
#include "src/concurrent/work_queue.h"
#include "src/cpp_command.h"
#include "src/file_link_mode.h"
#include "src/find_mode.h"
#include "src/goto_command.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/extended_char.h"
#include "src/infrastructure/file_descriptor_reader.h"
#include "src/infrastructure/time.h"
#include "src/insert_mode.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/text/line_column.h"
#include "src/language/wstring.h"
#include "src/line_marks_buffer.h"
#include "src/line_prompt_mode.h"
#include "src/map_mode.h"
#include "src/navigate_command.h"
#include "src/navigation_buffer.h"
#include "src/open_directory_command.h"
#include "src/open_file_command.h"
#include "src/operation.h"
#include "src/parse_tree.h"
#include "src/quit_command.h"
#include "src/record_command.h"
#include "src/repeat_mode.h"
#include "src/run_command_handler.h"
#include "src/run_cpp_command.h"
#include "src/run_cpp_file.h"
#include "src/search_command.h"
#include "src/search_handler.h"
#include "src/seek.h"
#include "src/set_variable_command.h"
#include "src/terminal.h"
#include "src/transformation.h"
#include "src/transformation/composite.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/switch_case.h"
#include "src/transformation/type.h"

namespace gc = afc::language::gc;

using afc::concurrent::WorkQueue;
using afc::infrastructure::AddSeconds;
using afc::infrastructure::ControlChar;
using afc::infrastructure::ExtendedChar;
using afc::infrastructure::Now;
using afc::infrastructure::Path;
using afc::infrastructure::PathComponent;
using afc::infrastructure::VectorExtendedChar;
using afc::language::EmptyValue;
using afc::language::Error;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::overload;
using afc::language::Success;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::SingleLine;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::OutgoingLink;

namespace afc::editor {
namespace {

class UndoCommand : public Command {
 public:
  UndoCommand(EditorState& editor_state, std::optional<Direction> direction)
      : editor_state_(editor_state), direction_(direction) {}

  LazyString Description() const override {
    switch (direction_.value_or(Direction::kForwards)) {
      case Direction::kBackwards:
        return LazyString{L"re-does the last change to the current buffer"};
      case Direction::kForwards:
        return LazyString{L"un-does the last change to the current buffer"};
    }
    LOG(FATAL) << "Invalid direction value.";
    return LazyString{};
  }

  LazyString Category() const override { return LazyString{L"Edit"}; }

  void ProcessInput(ExtendedChar) override {
    if (direction_.has_value()) {
      editor_state_.set_direction(direction_.value());
    }
    editor_state_
        .ForEachActiveBuffer([](OpenBuffer& buffer) {
          return buffer.Undo(UndoState::ApplyOptions::Mode::kLoop,
                             UndoState::ApplyOptions::RedoMode::kPopulate);
        })
        .Transform([&editor_state = editor_state_](EmptyValue) {
          editor_state.ResetRepetitions();
          editor_state.ResetDirection();
          return EmptyValue();
        });
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
  const std::optional<Direction> direction_;
};

class GotoPreviousPositionCommand : public Command {
 public:
  GotoPreviousPositionCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"go back to previous position"};
  }
  LazyString Category() const override { return LazyString{L"Navigate"}; }

  void ProcessInput(ExtendedChar) override {
    if (!editor_state_.HasPositionsInStack()) {
      LOG(INFO) << "Editor doesn't have positions in stack.";
      return;
    }
    while (editor_state_.repetitions().value_or(1) > 0) {
      if (!editor_state_.MovePositionsStack(editor_state_.direction())) {
        LOG(INFO) << "Editor failed to move in positions stack.";
        return;
      }
      const BufferPosition pos = editor_state_.ReadPositionsStack();
      auto it = editor_state_.buffers()->find(pos.buffer_name);
      auto current_buffer = editor_state_.current_buffer();
      // TODO(easy, 2022-05-15): Why is this safe?
      CHECK(current_buffer.has_value());
      const LineColumn current_position =
          editor_state_.current_buffer()->ptr()->position();
      if (it != editor_state_.buffers()->end() &&
          (pos.buffer_name != current_buffer->ptr()->name() ||
           ((editor_state_.structure() == Structure::kLine ||
             editor_state_.structure() == Structure::kWord ||
             editor_state_.structure() == Structure::kSymbol ||
             editor_state_.structure() == Structure::kChar) &&
            pos.position.line != current_position.line) ||
           (editor_state_.structure() == Structure::kChar &&
            pos.position.column != current_position.column))) {
        LOG(INFO) << "Jumping to position: "
                  << it->second.ptr()->Read(buffer_variables::name) << " "
                  << pos.position;
        editor_state_.set_current_buffer(it->second,
                                         CommandArgumentModeApplyMode::kFinal);
        it->second.ptr()->set_position(pos.position);
        editor_state_.set_repetitions(editor_state_.repetitions().value_or(1) -
                                      1);
      }
    }
    editor_state_.ResetDirection();
    editor_state_.ResetRepetitions();
    editor_state_.ResetStructure();
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

class EnterInsertModeCommand : public Command {
 public:
  EnterInsertModeCommand(EditorState& editor_state,
                         std::optional<Modifiers> modifiers)
      : editor_state_(editor_state), modifiers_(std::move(modifiers)) {}

  LazyString Description() const override {
    return LazyString{L"enters insert mode"};
  }
  LazyString Category() const override { return LazyString{L"Edit"}; }

  void ProcessInput(ExtendedChar) override {
    if (modifiers_.has_value()) {
      editor_state_.set_modifiers(modifiers_.value());
    }
    EnterInsertMode(InsertModeOptions{.editor_state = editor_state_});
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
  const std::optional<Modifiers> modifiers_;
};

class InsertionModifierCommand : public Command {
 public:
  InsertionModifierCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{
        L"activates replace modifier (overwrites text on insertions)"};
  }
  LazyString Category() const override { return LazyString{L"Modifiers"}; }

  void ProcessInput(ExtendedChar) override {
    if (editor_state_.insertion_modifier() == Modifiers::ModifyMode::kShift) {
      editor_state_.set_insertion_modifier(Modifiers::ModifyMode::kOverwrite);
    } else if (editor_state_.default_insertion_modifier() ==
               Modifiers::ModifyMode::kShift) {
      editor_state_.set_default_insertion_modifier(
          Modifiers::ModifyMode::kOverwrite);
    } else {
      editor_state_.set_default_insertion_modifier(
          Modifiers::ModifyMode::kShift);
      editor_state_.ResetInsertionModifier();
    }
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

class SetStructureCommand : public Command {
 public:
  SetStructureCommand(EditorState& editor_state, Structure structure)
      : editor_state_(editor_state), structure_(structure) {}

  LazyString Description() const override {
    std::ostringstream os;
    os << structure_;
    return LazyString{L"sets the structure: "} +
           LazyString{language::FromByteString(os.str())};
  }
  LazyString Category() const override { return LazyString{L"Modifiers"}; }

  void ProcessInput(ExtendedChar) override {
    if (editor_state_.structure() != structure_) {
      editor_state_.set_structure(structure_);
      editor_state_.set_sticky_structure(false);
    } else if (!editor_state_.sticky_structure()) {
      editor_state_.set_sticky_structure(true);
    } else {
      editor_state_.set_structure(Structure::kChar);
      editor_state_.set_sticky_structure(false);
    }
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
  Structure structure_;
};

class SetStrengthCommand : public Command {
 public:
  SetStrengthCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"Toggles the strength."};
  }
  LazyString Category() const override { return LazyString{L"Modifiers"}; }

  void ProcessInput(ExtendedChar) override {
    Modifiers modifiers(editor_state_.modifiers());
    switch (modifiers.strength) {
      case Modifiers::Strength::kNormal:
        modifiers.strength = Modifiers::Strength::kStrong;
        break;
      case Modifiers::Strength::kStrong:
        modifiers.strength = Modifiers::Strength::kNormal;
        break;
    }
    editor_state_.set_modifiers(modifiers);
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

class NumberMode : public Command {
  EditorState& editor_state_;

 public:
  NumberMode(EditorState& editor_state) : editor_state_(editor_state) {}

  LazyString Description() const override {
    return LazyString{L"sets repetitions for the next command."};
  }
  LazyString Category() const override { return LazyString{L"Modifiers"}; }

  void ProcessInput(ExtendedChar c) override {
    editor_state_.set_keyboard_redirect(NewRepeatMode(
        editor_state_, [&editor_state = editor_state_](int number) {
          editor_state.set_repetitions(number);
        }));
    editor_state_.ProcessInput({c});
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }
};

class ActivateLink : public Command {
 public:
  ActivateLink(EditorState& editor_state) : editor_state_(editor_state) {}
  LazyString Description() const override {
    return LazyString{L"activates the current link (if any)"};
  }
  LazyString Category() const override { return LazyString{L"Navigate"}; }

  void ProcessInput(ExtendedChar) override {
    VisitPointer(
        editor_state_.current_buffer(),
        [&](gc::Root<OpenBuffer> buffer) {
          VisitPointer(
              buffer.ptr()->CurrentLine().outgoing_link(),
              [&](OutgoingLink outgoing_link) {
                if (std::optional<gc::Root<OpenBuffer>> target_link =
                        buffer.ptr()->editor().buffer_registry().FindPath(
                            outgoing_link.path);
                    target_link.has_value() &&
                    &target_link->ptr().value() != &buffer.ptr().value()) {
                  LOG(INFO)
                      << "Visiting buffer: " << target_link->ptr()->name();
                  editor_state_.status().Reset();
                  buffer.ptr()->status().Reset();
                  editor_state_.set_current_buffer(
                      target_link.value(),
                      CommandArgumentModeApplyMode::kFinal);
                  std::optional<LineColumn> target_position =
                      outgoing_link.line_column;
                  if (target_position.has_value())
                    target_link->ptr()->set_position(*target_position);
                  editor_state_.PushCurrentPosition();
                  buffer.ptr()->ResetMode();
                  target_link->ptr()->ResetMode();
                  return;
                }
              },
              [] {});

          buffer.ptr()->MaybeAdjustPositionCol();
          buffer.ptr()
              ->OpenBufferForCurrentPosition(
                  OpenBuffer::RemoteURLBehavior::kLaunchBrowser)
              .Transform(
                  [&editor_state = editor_state_](
                      std::optional<gc::Root<OpenBuffer>> optional_target) {
                    return VisitPointer(
                        optional_target,
                        [&](gc::Root<OpenBuffer> target) {
                          if (LazyString path = target.ptr()->ReadLazyString(
                                  buffer_variables::path);
                              !path.IsEmpty())
                            AddLineToHistory(editor_state, HistoryFileFiles(),
                                             path);
                          editor_state.AddBuffer(
                              target, BuffersList::AddBufferType::kVisit);
                          return Success();
                        },
                        [] { return Success(); });
                  });
        },
        [] {});
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

class ResetStateCommand : public Command {
 public:
  ResetStateCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  LazyString Description() const override {
    return LazyString{L"Resets the state of the editor."};
  }
  LazyString Category() const override { return LazyString{L"Editor"}; }

  void ProcessInput(ExtendedChar) override {
    editor_state_.status().Reset();
    editor_state_.ForEachActiveBuffer([](OpenBuffer& buffer) {
      buffer.work_queue()->DeleteLater(
          AddSeconds(Now(), 0.2), buffer.status().SetExpiringInformationText(
                                      Line{SingleLine{LazyString{L"❌ ESC"}}}));
      return futures::Past(EmptyValue());
    });
    editor_state_.set_modifiers(Modifiers());
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

class HardRedrawCommand : public Command {
 public:
  HardRedrawCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  LazyString Description() const override {
    return LazyString{L"Redraws the screen"};
  }
  LazyString Category() const override { return LazyString{L"View"}; }

  void ProcessInput(ExtendedChar) override {
    editor_state_.set_screen_needs_hard_redraw(true);
  }

  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> Expand()
      const override {
    return {};
  }

 private:
  EditorState& editor_state_;
};

enum class VariableLocation { kBuffer, kEditor };

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<bool>* variable,
                    MapModeCommands& map_mode) {
  LazyString name = variable->name();
  LazyString command;
  switch (variable_location) {
    case VariableLocation::kBuffer:
      command =
          LazyString{L"// Variables: Toggle buffer variable (bool): "} + name +
          LazyString{
              L"\neditor.ForEachActiveBuffer([](Buffer buffer) -> void {\n"
              L"buffer.set_"} +
          name + LazyString{L"(editor.repetitions() == 0 ? false : !buffer."} +
          name + LazyString{L"()); buffer.SetStatus((buffer."} + name +
          LazyString{L"() ? \"🗸\" : \"⛶\") + \" "} + name +
          LazyString{L"\"); }); editor.set_repetitions(1);"};
      break;
    case VariableLocation::kEditor:
      command = LazyString{L"// Variables: Toggle editor variable: "} + name +
                LazyString{L"\neditor.set_"} + name +
                LazyString{L"(editor.repetitions() == 0 ? false : !editor."} +
                name + LazyString{L"()); editor.SetStatus((editor."} + name +
                LazyString{L"() ? \"🗸\" : \"⛶\") + \" "} + name +
                LazyString{L"\"); editor.set_repetitions(1);"};
      break;
  }
  VLOG(5) << "Command: " << command;
  std::visit(
      overload{[](Error error) {
                 LOG(FATAL)
                     << "Internal error in ToggleVariable code: " << error;
               },
               [&](gc::Root<Command> value) {
                 map_mode.Add(VectorExtendedChar(LazyString{L"v"} +
                                                 LazyString{variable->key()}),
                              value.ptr());
               }},
      NewCppCommand(editor_state, editor_state.environment(), command));
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<LazyString>* variable,
                    MapModeCommands& map_mode) {
  LazyString name = variable->name();
  LazyString command;
  switch (variable_location) {
    case VariableLocation::kBuffer:
      command = LazyString{L"// Variables: Toggle buffer variable (string): "} +
                name + LazyString{L"\neditor.SetVariablePrompt(\""} + name +
                LazyString{L"\");"};
      break;
    case VariableLocation::kEditor:
      // TODO: Implement.
      CHECK(false) << "Not implemented.";
      break;
  }
  VLOG(5) << "Command: " << command;
  map_mode.Add(
      VectorExtendedChar(LazyString{L"v"} + LazyString{variable->key()}),
      ValueOrDie(
          NewCppCommand(editor_state, editor_state.environment(), command),
          L"ToggleVariable<LazyString> Definition")
          .ptr());
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<int>* variable,
                    MapModeCommands& map_mode) {
  LazyString name = variable->name();
  LazyString command;
  switch (variable_location) {
    case VariableLocation::kBuffer:
      command =
          LazyString{L"// Variables: Toggle buffer variable (int): "} + name +
          LazyString{
              L"\neditor.ForEachActiveBuffer([](Buffer buffer) -> void {\n"
              L"buffer.set_"} +
          name + LazyString{L"(editor.repetitions());buffer.SetStatus(\""} +
          name + LazyString{L" := \" + buffer."} + name +
          LazyString{L"().tostring()); }); editor.set_repetitions(1);\n"};
      break;
    case VariableLocation::kEditor:
      command =
          LazyString{L"// Variables: Toggle editor variable (int): "} + name +
          LazyString{L"\neditor.set_"} + name +
          LazyString{L"(editor.repetitions());editor.SetStatus(\"editor."} +
          name + LazyString{L" := \" + editor."} + name +
          LazyString{L"().tostring());editor.set_repetitions(1);\n"};
      break;
  }
  VLOG(5) << "Command: " << command;
  map_mode.Add(
      VectorExtendedChar(LazyString{L"v"} + LazyString{variable->key()}),
      ValueOrDie(
          NewCppCommand(editor_state, editor_state.environment(), command),
          L"ToggleVariable<int> definition")
          .ptr());
}

template <typename T>
void RegisterVariableKeys(EditorState& editor_state, EdgeStruct<T>* edge_struct,
                          VariableLocation variable_location,
                          MapModeCommands& map_mode) {
  for (const LazyString& name : edge_struct->VariableNames()) {
    const EdgeVariable<T>* variable = edge_struct->find_variable(name);
    CHECK(variable != nullptr);
    if (!variable->key().empty()) {
      ToggleVariable(editor_state, variable_location, variable, map_mode);
    }
  }
}
}  // namespace

gc::Root<MapModeCommands> NewCommandMode(EditorState& editor_state) {
  gc::Root<MapModeCommands> commands_root = MapModeCommands::New(editor_state);
  MapModeCommands& commands = commands_root.ptr().value();
  commands.Add({L'a', L'q'}, NewQuitCommand(editor_state, 0).ptr());
  commands.Add({L'a', L'Q'}, NewQuitCommand(editor_state, 1).ptr());
  commands.Add({L'a', L'v'}, NewSetVariableCommand(editor_state).ptr());
  commands.Add({L'a', L'c'}, NewRunCppFileCommand(editor_state).ptr());
  commands.Add({L'a', L'C'},
               NewRunCppCommand(editor_state, CppCommandMode::kLiteral).ptr());
  commands.Add({L':'},
               NewRunCppCommand(editor_state, CppCommandMode::kShell).ptr());
  commands.Add({L'a', L'.'}, NewOpenDirectoryCommand(editor_state).ptr());
  commands.Add({L'a', L'o'}, NewOpenFileCommand(editor_state).ptr());
  commands.Add(
      {L'a', L'F'},
      NewLinePromptCommand(
          editor_state, L"forks a command for each line in the current buffer",
          [&editor_state] {
            return PromptOptions{
                .editor_state = editor_state,
                .prompt = LazyString{L"...$ "},
                .history_file = HistoryFileCommands(),
                .handler = std::bind_front(RunMultipleCommandsHandler,
                                           std::ref(editor_state))};
          })
          .ptr());

  commands.Add({L'a', L'f'}, NewForkCommand(editor_state).ptr());

  commands.Add({L'a', L'm'}, NewLineMarksBufferCommand(editor_state).ptr());

  commands.Add({L'N'}, NewNavigationBufferCommand(editor_state).ptr());
  commands.Add({L'i'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<EnterInsertModeCommand>(
                               editor_state, std::nullopt))
                           .ptr());
  commands.Add({L'I'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<EnterInsertModeCommand>(
                               editor_state,
                               [] {
                                 Modifiers output;
                                 output.insertion =
                                     Modifiers::ModifyMode::kOverwrite;
                                 return output;
                               }()))
                           .ptr());

  commands.Add({L'f'},
               operation::NewTopLevelCommand(
                   L"find",
                   LazyString{L"reaches the next occurrence of a specific "
                              L"character in the current line"},
                   operation::TopCommand(), editor_state,
                   {operation::CommandReachQuery{}})
                   .ptr());
  commands.Add({L'r'}, operation::NewTopLevelCommand(
                           L"reach", LazyString{L"starts a new reach command"},
                           operation::TopCommand(), editor_state, {})
                           .ptr());

  commands.Add(
      {L'R'},
      editor_state.gc_pool()
          .NewRoot(MakeNonNullUnique<InsertionModifierCommand>(editor_state))
          .ptr());

  commands.Add({L'/'}, NewSearchCommand(editor_state).ptr());
  commands.Add({L'g'}, NewGotoCommand(editor_state).ptr());

  commands.Add({L'W'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kSymbol))
                           .ptr());
  commands.Add({L'w'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kWord))
                           .ptr());
  commands.Add({L'E'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kPage))
                           .ptr());
  commands.Add({L'c'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kCursor))
                           .ptr());
  commands.Add({L'B'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kBuffer))
                           .ptr());
  commands.Add({L'!'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kMark))
                           .ptr());
  commands.Add({L't'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                               editor_state, Structure::kTree))
                           .ptr());

  commands.Add(
      {L'e'}, operation::NewTopLevelCommand(
                  L"delete", LazyString{L"starts a new delete command"},
                  operation::TopCommand{
                      .post_transformation_behavior = transformation::Stack::
                          PostTransformationBehavior::kDeleteRegion},
                  editor_state,
                  {operation::CommandReach{
                      .repetitions = operation::CommandArgumentRepetitions(1)}})
                  .ptr());
  commands.Add(
      {L'p'},
      operation::NewTopLevelCommand(
          L"paste", LazyString{L"paste from the delete buffer"},
          operation::TopCommand(), editor_state, {operation::CommandPaste{}})
          .ptr());

  commands.Add({L'u'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<UndoCommand>(
                               editor_state, std::nullopt))
                           .ptr());
  commands.Add({L'U'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<UndoCommand>(
                               editor_state, Direction::kBackwards))
                           .ptr());
  commands.Add({L'\n'},
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<ActivateLink>(editor_state))
                   .ptr());

  commands.Add(
      {L'b'},
      editor_state.gc_pool()
          .NewRoot(MakeNonNullUnique<GotoPreviousPositionCommand>(editor_state))
          .ptr());
  commands.Add({L'n'}, NewNavigateCommand(editor_state).ptr());

  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'j', ControlChar::kDownArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"down", LazyString{L"moves down one line"},
                 operation::TopCommand(), editor_state,
                 {operation::CommandReachLine{
                     .repetitions = operation::CommandArgumentRepetitions(1)}})
                 .ptr());
  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'k', ControlChar::kUpArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"up", LazyString{L"moves up one line"},
                 operation::TopCommand(), editor_state,
                 {operation::CommandReachLine{
                     .repetitions = operation::CommandArgumentRepetitions(-1)}})
                 .ptr());

  // commands.Add({L'j'}, std::make_unique<LineDown>());
  // commands.Add({L'k'}, std::make_unique<LineUp>());
  // commands.Add({L'l'},
  // std::make_unique<MoveForwards>(Direction::kForwards));
  // commands.Add({L'h'},
  // std::make_unique<MoveForwards>(Direction::kBackwards));
  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'l', ControlChar::kRightArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"right", LazyString{L"moves right one position"},
                 operation::TopCommand(), editor_state,
                 {operation::CommandReach{
                     .repetitions = operation::CommandArgumentRepetitions(1)}})
                 .ptr());
  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'h', ControlChar::kLeftArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"left", LazyString{L"moves left one position"},
                 operation::TopCommand(), editor_state,
                 {operation::CommandReach{
                     .repetitions = operation::CommandArgumentRepetitions(-1)}})
                 .ptr());

  for (ExtendedChar x : std::vector<ExtendedChar>({L'H', ControlChar::kHome}))
    commands.Add(
        {x},
        operation::NewTopLevelCommand(
            L"home", LazyString{L"moves to the beginning of the current line"},
            operation::TopCommand(), editor_state,
            {operation::CommandReachBegin{
                .structure = Structure::kChar,
                .repetitions = operation::CommandArgumentRepetitions(1)}})
            .ptr());

  for (ExtendedChar x : std::vector<ExtendedChar>({L'L', ControlChar::kEnd}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"end", LazyString{L"moves to the end of the current line"},
                 operation::TopCommand(), editor_state,
                 {operation::CommandReachBegin{
                     .structure = Structure::kChar,
                     .repetitions = operation::CommandArgumentRepetitions(1),
                     .direction = Direction::kBackwards}})
                 .ptr());
  commands.Add(
      {L'K'}, operation::NewTopLevelCommand(
                  L"file-home",
                  LazyString{L"moves to the beginning of the current file"},
                  operation::TopCommand(), editor_state,
                  {operation::CommandReachBegin{
                      .structure = Structure::kLine,
                      .repetitions = operation::CommandArgumentRepetitions(1)}})
                  .ptr());
  commands.Add(
      {L'J'},
      operation::NewTopLevelCommand(
          L"file-end", LazyString{L"moves to the end of the current file"},
          operation::TopCommand(), editor_state,
          {operation::CommandReachBegin{
              .structure = Structure::kLine,
              .repetitions = operation::CommandArgumentRepetitions(1),
              .direction = Direction::kBackwards}})
          .ptr());
  commands.Add(
      {L'~'}, operation::NewTopLevelCommand(
                  L"switch-case",
                  LazyString{L"switches the case of the current character."},
                  operation::TopCommand{
                      .post_transformation_behavior = transformation::Stack::
                          PostTransformationBehavior::kCapitalsSwitch},
                  editor_state,
                  {operation::CommandReach{
                      .repetitions = operation::CommandArgumentRepetitions(1)}})
                  .ptr());

  commands.Add(
      {L'%'}, operation::NewTopLevelCommand(
                  L"tree-navigate",
                  LazyString{L"moves past the next token in the syntax tree"},
                  operation::TopCommand{}, editor_state,
                  {operation::CommandReach{
                      .structure = Structure::kTree,
                      .repetitions = operation::CommandArgumentRepetitions(1)}})
                  .ptr());

  commands.Add({L's', L'r'}, NewRecordCommand(editor_state).ptr());
  commands.Add({L'\t'}, NewFindCompletionCommand(editor_state).ptr());

  RegisterVariableKeys(editor_state, editor_variables::BoolStruct(),
                       VariableLocation::kEditor, commands_root.ptr().value());
  RegisterVariableKeys(editor_state, editor_variables::IntStruct(),
                       VariableLocation::kEditor, commands_root.ptr().value());
  RegisterVariableKeys(editor_state, buffer_variables::BoolStruct(),
                       VariableLocation::kBuffer, commands_root.ptr().value());
  RegisterVariableKeys(editor_state, buffer_variables::StringStruct(),
                       VariableLocation::kBuffer, commands_root.ptr().value());
  RegisterVariableKeys(editor_state, buffer_variables::IntStruct(),
                       VariableLocation::kBuffer, commands_root.ptr().value());

  commands.Add({ControlChar::kEscape},
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<ResetStateCommand>(editor_state))
                   .ptr());

  commands.Add({ControlChar::kCtrlL},
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<HardRedrawCommand>(editor_state))
                   .ptr());
  commands.Add({L'*'},
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStrengthCommand>(editor_state))
                   .ptr());
  commands.Add({L'0'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'1'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'2'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'3'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'4'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'5'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'6'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'7'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'8'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());
  commands.Add({L'9'}, editor_state.gc_pool()
                           .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                           .ptr());

  commands.Add(
      {ControlChar::kPageDown},
      operation::NewTopLevelCommand(
          L"page_down", LazyString{L"moves down one page"},
          operation::TopCommand(), editor_state,
          {operation::CommandReachPage{
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());
  commands.Add(
      {ControlChar::kPageUp},
      operation::NewTopLevelCommand(
          L"page_up", LazyString{L"moves up one page"}, operation::TopCommand(),
          editor_state,
          {operation::CommandReachPage{
              .repetitions = operation::CommandArgumentRepetitions(-1)}})
          .ptr());
  return commands_root;
}

}  // namespace afc::editor
