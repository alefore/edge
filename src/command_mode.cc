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
#include "src/buffer_variables.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/completion_model.h"
#include "src/concurrent/work_queue.h"
#include "src/cpp_command.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/find_mode.h"
#include "src/goto_command.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/extended_char.h"
#include "src/infrastructure/time.h"
#include "src/insert_mode.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/overload.h"
#include "src/language/text/line_column.h"
#include "src/language/wstring.h"
#include "src/line_prompt_mode.h"
#include "src/map_mode.h"
#include "src/navigate_command.h"
#include "src/navigation_buffer.h"
#include "src/open_directory_command.h"
#include "src/open_file_command.h"
#include "src/operation.h"
#include "src/parse_tree.h"
#include "src/paste.h"
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
using afc::language::ToByteString;
using afc::language::ValueOrError;
using afc::language::VisitPointer;
using afc::language::lazy_string::NewLazyString;
using afc::language::text::Line;
using afc::language::text::LineColumn;
using afc::language::text::OutgoingLink;

namespace afc::editor {
namespace {

class UndoCommand : public Command {
 public:
  UndoCommand(EditorState& editor_state, std::optional<Direction> direction)
      : editor_state_(editor_state), direction_(direction) {}

  std::wstring Description() const override {
    switch (direction_.value_or(Direction::kForwards)) {
      case Direction::kBackwards:
        return L"re-does the last change to the current buffer";
      case Direction::kForwards:
        return L"un-does the last change to the current buffer";
    }
    LOG(FATAL) << "Invalid direction value.";
    return L"";
  }

  std::wstring Category() const override { return L"Edit"; }

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

  std::wstring Description() const override {
    return L"go back to previous position";
  }
  std::wstring Category() const override { return L"Navigate"; }

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

  std::wstring Description() const override { return L"enters insert mode"; }
  std::wstring Category() const override { return L"Edit"; }

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

  std::wstring Description() const override {
    return L"activates replace modifier (overwrites text on insertions)";
  }
  std::wstring Category() const override { return L"Modifiers"; }

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

  std::wstring Description() const override {
    std::ostringstream os;
    os << structure_;
    return L"sets the structure: " + language::FromByteString(os.str());
  }
  std::wstring Category() const override { return L"Modifiers"; }

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
  const std::wstring description_;
};

class SetStrengthCommand : public Command {
 public:
  SetStrengthCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  std::wstring Description() const override { return L"Toggles the strength."; }
  std::wstring Category() const override { return L"Modifiers"; }

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
 public:
  NumberMode(EditorState& editor_state) : NumberMode(editor_state, L"") {}
  NumberMode(EditorState& editor_state, const std::wstring& description)
      : editor_state_(editor_state), description_(description) {}

  std::wstring Description() const override { return description_; }
  std::wstring Category() const override { return L"Modifiers"; }

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

 private:
  EditorState& editor_state_;
  const std::wstring description_;
};

class ActivateLink : public Command {
 public:
  ActivateLink(EditorState& editor_state) : editor_state_(editor_state) {}
  std::wstring Description() const override {
    return L"activates the current link (if any)";
  }
  std::wstring Category() const override { return L"Navigate"; }

  void ProcessInput(ExtendedChar) override {
    VisitPointer(
        editor_state_.current_buffer(),
        [&](gc::Root<OpenBuffer> buffer) {
          VisitPointer(
              buffer.ptr()->CurrentLine()->outgoing_link(),
              [&](OutgoingLink outgoing_link) {
                if (auto it = buffer.ptr()->editor().buffers()->find(
                        BufferName(outgoing_link.path));
                    it != buffer.ptr()->editor().buffers()->end() &&
                    &it->second.ptr().value() != &buffer.ptr().value()) {
                  LOG(INFO) << "Visiting buffer: " << it->second.ptr()->name();
                  editor_state_.status().Reset();
                  buffer.ptr()->status().Reset();
                  editor_state_.set_current_buffer(
                      it->second, CommandArgumentModeApplyMode::kFinal);
                  std::optional<LineColumn> target_position =
                      outgoing_link.line_column;
                  if (target_position.has_value()) {
                    it->second.ptr()->set_position(*target_position);
                  }
                  editor_state_.PushCurrentPosition();
                  buffer.ptr()->ResetMode();
                  it->second.ptr()->ResetMode();
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
                          if (std::wstring path =
                                  target.ptr()->Read(buffer_variables::path);
                              !path.empty())
                            AddLineToHistory(editor_state, HistoryFileFiles(),
                                             NewLazyString(path));
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
  std::wstring Description() const override {
    return L"Resets the state of the editor.";
  }
  std::wstring Category() const override { return L"Editor"; }

  void ProcessInput(ExtendedChar) override {
    editor_state_.status().Reset();
    editor_state_.ForEachActiveBuffer([](OpenBuffer& buffer) {
      buffer.work_queue()->DeleteLater(
          AddSeconds(Now(), 0.2),
          buffer.status().SetExpiringInformationText(
              MakeNonNullShared<Line>(Line(L"❌ ESC"))));
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
  std::wstring Description() const override { return L"Redraws the screen"; }
  std::wstring Category() const override { return L"View"; }

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
  auto name = variable->name();
  std::wstring command;
  switch (variable_location) {
    case VariableLocation::kBuffer:
      command = L"// Variables: Toggle buffer variable (bool): " + name +
                L"\neditor.ForEachActiveBuffer([](Buffer buffer) -> void {\n"
                L"buffer.set_" +
                name + L"(editor.repetitions() == 0 ? false : !buffer." + name +
                L"()); buffer.SetStatus((buffer." + name +
                L"() ? \"🗸\" : \"⛶\") + \" " + name +
                L"\"); }); editor.set_repetitions(1);";
      break;
    case VariableLocation::kEditor:
      command = L"// Variables: Toggle editor variable: " + name +
                L"\neditor.set_" + name +
                L"(editor.repetitions() == 0 ? false : !editor." + name +
                L"()); editor.SetStatus((editor." + name +
                L"() ? \"🗸\" : \"⛶\") + \" " + name +
                L"\"); editor.set_repetitions(1);";
      break;
  }
  VLOG(5) << "Command: " << command;
  std::visit(overload{[](Error error) {
                        LOG(FATAL) << "Internal error in ToggleVariable code: "
                                   << error;
                      },
                      [&](gc::Root<Command> value) {
                        map_mode.Add(VectorExtendedChar(L"v" + variable->key()),
                                     value.ptr());
                      }},
             NewCppCommand(editor_state, editor_state.environment(), command));
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<std::wstring>* variable,
                    MapModeCommands& map_mode) {
  auto name = variable->name();
  std::wstring command;
  switch (variable_location) {
    case VariableLocation::kBuffer:
      command = L"// Variables: Toggle buffer variable (string): " + name +
                L"\neditor.SetVariablePrompt(\"" + name + L"\");";
      break;
    case VariableLocation::kEditor:
      // TODO: Implement.
      CHECK(false) << "Not implemented.";
      break;
  }
  VLOG(5) << "Command: " << command;
  map_mode.Add(VectorExtendedChar(L"v" + variable->key()),
               ValueOrDie(NewCppCommand(editor_state,
                                        editor_state.environment(), command),
                          L"ToggleVariable<std::wstring> Definition")
                   .ptr());
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<int>* variable,
                    MapModeCommands& map_mode) {
  // TODO: Honor variable_location.
  auto name = variable->name();
  std::wstring command;
  switch (variable_location) {
    case VariableLocation::kBuffer:
      command = L"// Variables: Toggle buffer variable (int): " + name +
                L"\neditor.ForEachActiveBuffer([](Buffer buffer) -> void {\n"
                L"buffer.set_" +
                name + L"(editor.repetitions());buffer.SetStatus(\"" + name +
                L" := \" + buffer." + name +
                L"().tostring()); }); editor.set_repetitions(1);\n";
      break;
    case VariableLocation::kEditor:
      command = L"// Variables: Toggle editor variable (int): " + name +
                L"\neditor.set_" + name +
                L"(editor.repetitions());editor.SetStatus(\"editor." + name +
                L" := \" + editor." + name +
                L"().tostring());editor.set_repetitions(1);\n";
      break;
  }
  VLOG(5) << "Command: " << command;
  map_mode.Add(VectorExtendedChar(L"v" + variable->key()),
               ValueOrDie(NewCppCommand(editor_state,
                                        editor_state.environment(), command),
                          L"ToggleVariable<int> definition")
                   .ptr());
}

template <typename T>
void RegisterVariableKeys(EditorState& editor_state, EdgeStruct<T>* edge_struct,
                          VariableLocation variable_location,
                          MapModeCommands& map_mode) {
  for (const std::wstring& name : edge_struct->VariableNames()) {
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
  commands.Add(VectorExtendedChar(L"aq"),
               NewQuitCommand(editor_state, 0).ptr());
  commands.Add(VectorExtendedChar(L"aQ"),
               NewQuitCommand(editor_state, 1).ptr());
  commands.Add(VectorExtendedChar(L"av"),
               NewSetVariableCommand(editor_state).ptr());
  commands.Add(VectorExtendedChar(L"ac"),
               NewRunCppFileCommand(editor_state).ptr());
  commands.Add(VectorExtendedChar(L"aC"),
               NewRunCppCommand(editor_state, CppCommandMode::kLiteral).ptr());
  commands.Add(VectorExtendedChar(L":"),
               NewRunCppCommand(editor_state, CppCommandMode::kShell).ptr());
  commands.Add(VectorExtendedChar(L"a."),
               NewOpenDirectoryCommand(editor_state).ptr());
  commands.Add(VectorExtendedChar(L"ao"),
               NewOpenFileCommand(editor_state).ptr());
  commands.Add(
      VectorExtendedChar(L"aF"),
      NewLinePromptCommand(
          editor_state, L"forks a command for each line in the current buffer",
          [&editor_state] {
            return PromptOptions{
                .editor_state = editor_state,
                .prompt = NewLazyString(L"...$ "),
                .history_file = HistoryFileCommands(),
                .handler = std::bind_front(RunMultipleCommandsHandler,
                                           std::ref(editor_state))};
          })
          .ptr());

  commands.Add(VectorExtendedChar(L"af"), NewForkCommand(editor_state).ptr());

  commands.Add(VectorExtendedChar(L"N"),
               NewNavigationBufferCommand(editor_state).ptr());
  commands.Add(VectorExtendedChar(L"i"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<EnterInsertModeCommand>(
                       editor_state, std::nullopt))
                   .ptr());
  commands.Add(VectorExtendedChar(L"I"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<EnterInsertModeCommand>(
                       editor_state,
                       [] {
                         Modifiers output;
                         output.insertion = Modifiers::ModifyMode::kOverwrite;
                         return output;
                       }()))
                   .ptr());

  commands.Add(VectorExtendedChar(L"f"),
               operation::NewTopLevelCommand(
                   L"find",
                   L"reaches the next occurrence of a specific "
                   L"character in the current line",
                   operation::TopCommand(), editor_state,
                   {operation::CommandReachQuery{}})
                   .ptr());
  commands.Add(
      VectorExtendedChar(L"r"),
      operation::NewTopLevelCommand(L"reach", L"starts a new reach command",
                                    operation::TopCommand(), editor_state, {})
          .ptr());

  commands.Add(
      VectorExtendedChar(L"R"),
      editor_state.gc_pool()
          .NewRoot(MakeNonNullUnique<InsertionModifierCommand>(editor_state))
          .ptr());

  commands.Add(VectorExtendedChar(L"/"), NewSearchCommand(editor_state).ptr());
  commands.Add(VectorExtendedChar(L"g"), NewGotoCommand(editor_state).ptr());

  commands.Add(VectorExtendedChar(L"W"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kSymbol))
                   .ptr());
  commands.Add(VectorExtendedChar(L"w"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kWord))
                   .ptr());
  commands.Add(VectorExtendedChar(L"E"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kPage))
                   .ptr());
  commands.Add(VectorExtendedChar(L"c"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kCursor))
                   .ptr());
  commands.Add(VectorExtendedChar(L"B"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kBuffer))
                   .ptr());
  commands.Add(VectorExtendedChar(L"!"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kMark))
                   .ptr());
  commands.Add(VectorExtendedChar(L"t"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStructureCommand>(
                       editor_state, Structure::kTree))
                   .ptr());

  commands.Add(
      VectorExtendedChar(L"e"),
      operation::NewTopLevelCommand(
          L"delete", L"starts a new delete command",
          operation::TopCommand{
              .post_transformation_behavior = transformation::Stack::
                  PostTransformationBehavior::kDeleteRegion},
          editor_state,
          {operation::CommandReach{
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());
  commands.Add(VectorExtendedChar(L"p"), NewPasteCommand(editor_state).ptr());

  commands.Add(
      VectorExtendedChar(L"u"),
      editor_state.gc_pool()
          .NewRoot(MakeNonNullUnique<UndoCommand>(editor_state, std::nullopt))
          .ptr());
  commands.Add(VectorExtendedChar(L"U"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<UndoCommand>(
                       editor_state, Direction::kBackwards))
                   .ptr());
  commands.Add(VectorExtendedChar(L"\n"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<ActivateLink>(editor_state))
                   .ptr());

  commands.Add(
      VectorExtendedChar(L"b"),
      editor_state.gc_pool()
          .NewRoot(MakeNonNullUnique<GotoPreviousPositionCommand>(editor_state))
          .ptr());
  commands.Add(VectorExtendedChar(L"n"),
               NewNavigateCommand(editor_state).ptr());

  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'j', ControlChar::kDownArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"down", L"moves down one line", operation::TopCommand(),
                 editor_state,
                 {operation::CommandReachLine{
                     .repetitions = operation::CommandArgumentRepetitions(1)}})
                 .ptr());
  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'k', ControlChar::kUpArrow}))
    commands.Add(
        {x},
        operation::NewTopLevelCommand(
            L"up", L"moves up one line", operation::TopCommand(), editor_state,
            {operation::CommandReachLine{
                .repetitions = operation::CommandArgumentRepetitions(-1)}})
            .ptr());

  // commands.Add(VectorExtendedChar(L"j"), std::make_unique<LineDown>());
  // commands.Add(VectorExtendedChar(L"k"), std::make_unique<LineUp>());
  // commands.Add(VectorExtendedChar(L"l"),
  // std::make_unique<MoveForwards>(Direction::kForwards));
  // commands.Add(VectorExtendedChar(L"h"),
  // std::make_unique<MoveForwards>(Direction::kBackwards));
  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'l', ControlChar::kRightArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"right", L"moves right one position", operation::TopCommand(),
                 editor_state,
                 {operation::CommandReach{
                     .repetitions = operation::CommandArgumentRepetitions(1)}})
                 .ptr());
  for (ExtendedChar x :
       std::vector<ExtendedChar>({L'h', ControlChar::kLeftArrow}))
    commands.Add(
        {x}, operation::NewTopLevelCommand(
                 L"left", L"moves left one position", operation::TopCommand(),
                 editor_state,
                 {operation::CommandReach{
                     .repetitions = operation::CommandArgumentRepetitions(-1)}})
                 .ptr());
  commands.Add(
      VectorExtendedChar(L"H"),
      operation::NewTopLevelCommand(
          L"home", L"moves to the beginning of the current line",
          operation::TopCommand(), editor_state,
          {operation::CommandReachBegin{
              .structure = Structure::kChar,
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());
  commands.Add(VectorExtendedChar(L"L"),
               operation::NewTopLevelCommand(
                   L"end", L"moves to the end of the current line",
                   operation::TopCommand(), editor_state,
                   {operation::CommandReachBegin{
                       .structure = Structure::kChar,
                       .repetitions = operation::CommandArgumentRepetitions(1),
                       .direction = Direction::kBackwards}})
                   .ptr());
  commands.Add(
      VectorExtendedChar(L"K"),
      operation::NewTopLevelCommand(
          L"file-home", L"moves to the beginning of the current file",
          operation::TopCommand(), editor_state,
          {operation::CommandReachBegin{
              .structure = Structure::kLine,
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());
  commands.Add(VectorExtendedChar(L"J"),
               operation::NewTopLevelCommand(
                   L"file-end", L"moves to the end of the current file",
                   operation::TopCommand(), editor_state,
                   {operation::CommandReachBegin{
                       .structure = Structure::kLine,
                       .repetitions = operation::CommandArgumentRepetitions(1),
                       .direction = Direction::kBackwards}})
                   .ptr());
  commands.Add(
      VectorExtendedChar(L"~"),
      operation::NewTopLevelCommand(
          L"switch-case", L"Switches the case of the current character.",
          operation::TopCommand{
              .post_transformation_behavior = transformation::Stack::
                  PostTransformationBehavior::kCapitalsSwitch},
          editor_state,
          {operation::CommandReach{
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());

  commands.Add(
      VectorExtendedChar(L"%"),
      operation::NewTopLevelCommand(
          L"tree-navigate", L"moves past the next token in the syntax tree",
          operation::TopCommand{}, editor_state,
          {operation::CommandReach{
              .structure = Structure::kTree,
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());

  commands.Add(VectorExtendedChar(L"sr"), NewRecordCommand(editor_state).ptr());
  commands.Add(VectorExtendedChar(L"\t"),
               NewFindCompletionCommand(editor_state).ptr());

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
  commands.Add(VectorExtendedChar(L"*"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<SetStrengthCommand>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"0"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"1"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"2"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"3"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"4"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"5"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"6"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"7"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"8"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());
  commands.Add(VectorExtendedChar(L"9"),
               editor_state.gc_pool()
                   .NewRoot(MakeNonNullUnique<NumberMode>(editor_state))
                   .ptr());

  commands.Add(
      {ControlChar::kPageDown},
      operation::NewTopLevelCommand(
          L"page_down", L"moves down one page", operation::TopCommand(),
          editor_state,
          {operation::CommandReachPage{
              .repetitions = operation::CommandArgumentRepetitions(1)}})
          .ptr());
  commands.Add(
      {ControlChar::kPageUp},
      operation::NewTopLevelCommand(
          L"page_up", L"moves up one page", operation::TopCommand(),
          editor_state,
          {operation::CommandReachPage{
              .repetitions = operation::CommandArgumentRepetitions(-1)}})
          .ptr());
  return commands_root;
}

}  // namespace afc::editor
