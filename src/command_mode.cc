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
#include "src/char_buffer.h"
#include "src/command.h"
#include "src/command_argument_mode.h"
#include "src/command_with_modifiers.h"
#include "src/cpp_command.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/find_mode.h"
#include "src/goto_command.h"
#include "src/infrastructure/dirname.h"
#include "src/infrastructure/time.h"
#include "src/insert_mode.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/substring.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/line_column.h"
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
#include "src/transformation/tree_navigate.h"
#include "src/transformation/type.h"

namespace afc::editor {
namespace {

using infrastructure::AddSeconds;
using infrastructure::Now;
using language::EmptyValue;
using language::Error;
using language::MakeNonNullShared;
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;
using language::Success;
using language::ToByteString;
using language::ValueOrError;
using language::VisitPointer;

namespace gc = language::gc;

class UndoCommand : public Command {
 public:
  UndoCommand(EditorState& editor_state, std::optional<Direction> direction)
      : editor_state_(editor_state), direction_(direction) {}

  std::wstring Description() const override {
    if (direction_.value_or(Direction::kForwards) == Direction::kBackwards) {
      return L"re-does the last change to the current buffer";
    }
    return L"un-does the last change to the current buffer";
  }

  std::wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) override {
    if (direction_.has_value()) {
      editor_state_.set_direction(direction_.value());
    }
    editor_state_
        .ForEachActiveBuffer([](OpenBuffer& buffer) {
          return buffer.Undo(OpenBuffer::UndoMode::kLoop);
        })
        .SetConsumer([&editor_state = editor_state_](EmptyValue) {
          editor_state.ResetRepetitions();
          editor_state.ResetDirection();
        });
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

  void ProcessInput(wint_t) override {
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
           ((editor_state_.structure() == StructureLine() ||
             editor_state_.structure() == StructureWord() ||
             editor_state_.structure() == StructureSymbol() ||
             editor_state_.structure() == StructureChar()) &&
            pos.position.line != current_position.line) ||
           (editor_state_.structure() == StructureChar() &&
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

 private:
  EditorState& editor_state_;
};

class MoveForwards : public Command {
 public:
  MoveForwards(EditorState& editor_state, Direction direction)
      : editor_state_(editor_state), direction_(direction) {}

  std::wstring Description() const override {
    switch (direction_) {
      case Direction::kForwards:
        return L"moves forwards";
      case Direction::kBackwards:
        return L"moves backwards";
    }
    LOG(FATAL) << "Invalid direction value.";
    return L"";
  }

  std::wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t c) override { Move(c, editor_state_, direction_); }

  static void Move(int, EditorState& editor_state, Direction direction) {
    if (direction == Direction::kBackwards) {
      editor_state.set_direction(ReverseDirection(editor_state.direction()));
    }

    editor_state.ApplyToActiveBuffers(transformation::ModifiersAndComposite{
        editor_state.modifiers(), NewMoveTransformation()});
    editor_state.ResetRepetitions();
    editor_state.ResetStructure();
    editor_state.ResetDirection();
  }

 private:
  EditorState& editor_state_;
  const Direction direction_;
};

class LineDown : public Command {
 public:
  LineDown(EditorState& editor_state) : editor_state_(editor_state) {}
  std::wstring Description() const override { return L"moves down one line"; }
  std::wstring Category() const override { return L"Navigate"; }
  void ProcessInput(wint_t c) override {
    Move(c, editor_state_, editor_state_.structure());
  }
  static void Move(int c, EditorState& editor_state, Structure* structure) {
    // TODO: Move to Structure.
    if (structure == StructureChar()) {
      editor_state.set_structure(StructureLine());
      MoveForwards::Move(c, editor_state, Direction::kForwards);
    } else if (structure == StructureWord() || structure == StructureSymbol()) {
      editor_state.set_structure(StructurePage());
      MoveForwards::Move(c, editor_state, Direction::kForwards);
    } else if (structure == StructureTree()) {
      std::optional<gc::Root<OpenBuffer>> buffer =
          editor_state.current_buffer();
      if (!buffer.has_value()) {
        return;
      }
      switch (editor_state.direction()) {
        case Direction::kBackwards:
          if (buffer->ptr()->tree_depth() > 0) {
            buffer->ptr()->set_tree_depth(buffer->ptr()->tree_depth() - 1);
          }
          break;
        case Direction::kForwards: {
          NonNull<std::shared_ptr<const ParseTree>> root =
              buffer->ptr()->parse_tree();
          const ParseTree& tree = buffer->ptr()->current_tree(root.value());
          if (!tree.children().empty()) {
            buffer->ptr()->set_tree_depth(buffer->ptr()->tree_depth() + 1);
          }
          break;
        }
      }
      buffer->ptr()->ResetMode();
    } else {
      switch (editor_state.direction()) {
        case Direction::kForwards:
          editor_state.MoveBufferForwards(
              editor_state.repetitions().value_or(1));
          break;
        case Direction::kBackwards:
          editor_state.MoveBufferBackwards(
              editor_state.repetitions().value_or(1));
          break;
      }
    }
    editor_state.ResetStructure();
    editor_state.ResetRepetitions();
    editor_state.ResetDirection();
  }

 private:
  EditorState& editor_state_;
};

class LineUp : public Command {
 public:
  LineUp(EditorState& editor_state) : editor_state_(editor_state) {}
  std::wstring Description() const override { return L"moves up one line"; }
  std::wstring Category() const override { return L"Navigate"; }
  void ProcessInput(wint_t c) override {
    Move(c, editor_state_, editor_state_.structure());
  }
  static void Move(int c, EditorState& editor_state, Structure* structure) {
    editor_state.set_direction(ReverseDirection(editor_state.direction()));
    LineDown::Move(c, editor_state, structure);
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

  void ProcessInput(wint_t) override {
    if (modifiers_.has_value()) {
      editor_state_.set_modifiers(modifiers_.value());
    }
    EnterInsertMode({.editor_state = editor_state_});
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

  void ProcessInput(wint_t) override {
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

 private:
  EditorState& editor_state_;
};

class ReverseDirectionCommand : public Command {
 public:
  ReverseDirectionCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}

  std::wstring Description() const override {
    return L"reverses the direction of the next command";
  }
  std::wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) override {
    editor_state_.set_direction(ReverseDirection(editor_state_.direction()));
  }

 private:
  EditorState& editor_state_;
};

class SetStructureCommand : public Command {
 public:
  SetStructureCommand(EditorState& editor_state, Structure* structure)
      : editor_state_(editor_state), structure_(structure) {}

  std::wstring Description() const override {
    return L"sets the structure: " + structure_->ToString();
  }
  std::wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) override {
    if (editor_state_.structure() != structure_) {
      editor_state_.set_structure(structure_);
      editor_state_.set_sticky_structure(false);
    } else if (!editor_state_.sticky_structure()) {
      editor_state_.set_sticky_structure(true);
    } else {
      editor_state_.set_structure(StructureChar());
      editor_state_.set_sticky_structure(false);
    }
  }

 private:
  EditorState& editor_state_;
  Structure* structure_;
  const std::wstring description_;
};

class SetStrengthCommand : public Command {
 public:
  SetStrengthCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  std::wstring Description() const override { return L"Toggles the strength."; }
  std::wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) override {
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

  void ProcessInput(wint_t c) override {
    editor_state_.set_keyboard_redirect(NewRepeatMode(
        editor_state_, [&editor_state = editor_state_](int number) {
          editor_state.set_repetitions(number);
        }));
    editor_state_.ProcessInput(c);
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

  void ProcessInput(wint_t) override {
    VisitPointer(
        editor_state_.current_buffer(),
        [&](gc::Root<OpenBuffer> buffer) {
          // TODO(easy, 2022-05-18): Use VisitPointer as well.
          if (buffer.ptr()->current_line() == nullptr) {
            return;
          }

          VisitPointer(
              buffer.ptr()->current_line()->buffer_line_column(),
              [&](Line::BufferLineColumn line_buffer) {
                if (std::optional<gc::Root<OpenBuffer>> target =
                        line_buffer.buffer.Lock();
                    target.has_value() &&
                    &target->ptr().value() != &buffer.ptr().value()) {
                  LOG(INFO) << "Visiting buffer: "
                            << target->ptr()->Read(buffer_variables::name);
                  editor_state_.status().Reset();
                  buffer.ptr()->status().Reset();
                  editor_state_.set_current_buffer(
                      target.value(), CommandArgumentModeApplyMode::kFinal);
                  std::optional<LineColumn> target_position =
                      line_buffer.position;
                  if (target_position.has_value()) {
                    target->ptr()->set_position(*target_position);
                  }
                  editor_state_.PushCurrentPosition();
                  buffer.ptr()->ResetMode();
                  target->ptr()->ResetMode();
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

  void ProcessInput(wint_t) override {
    editor_state_.status().Reset();
    editor_state_.ForEachActiveBuffer([](OpenBuffer& buffer) {
      auto when = AddSeconds(Now(), 0.2);
      buffer.work_queue()->ScheduleAt(
          when, [status_expiration = std::shared_ptr<StatusExpirationControl>(
                     buffer.status().SetExpiringInformationText(L"ESC"))] {});
      return futures::Past(EmptyValue());
    });
    editor_state_.set_modifiers(Modifiers());
  }

 private:
  EditorState& editor_state_;
};

class HardRedrawCommand : public Command {
 public:
  HardRedrawCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  std::wstring Description() const override { return L"Redraws the screen"; }
  std::wstring Category() const override { return L"View"; }

  void ProcessInput(wint_t) override {
    editor_state_.set_screen_needs_hard_redraw(true);
  }

 private:
  EditorState& editor_state_;
};

class TreeNavigateCommand : public Command {
 public:
  TreeNavigateCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}
  std::wstring Description() const override {
    return L"Navigates to the start/end of the current children of the "
           L"syntax tree";
  }
  std::wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t) override {
    static const NonNull<std::shared_ptr<CompositeTransformation>>*
        transformation = new NonNull<std::shared_ptr<CompositeTransformation>>(
            MakeNonNullShared<TreeNavigate>());
    editor_state_.ApplyToActiveBuffers(*transformation);
  }

 private:
  EditorState& editor_state_;
};

enum class VariableLocation { kBuffer, kEditor };

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<bool>* variable,
                    MapModeCommands* map_mode) {
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
  LOG(INFO) << "Command: " << command;
  std::visit(overload{[](Error error) {
                        LOG(FATAL) << "Internal error in ToggleVariable code: "
                                   << error;
                      },
                      [&](NonNull<std::unique_ptr<Command>> value) {
                        map_mode->Add(L"v" + variable->key(), std::move(value));
                      }},
             NewCppCommand(editor_state, editor_state.environment(), command));
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<std::wstring>* variable,
                    MapModeCommands* map_mode) {
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
  LOG(INFO) << "Command: " << command;
  map_mode->Add(L"v" + variable->key(),
                ValueOrDie(NewCppCommand(editor_state,
                                         editor_state.environment(), command),
                           L"ToggleVariable<std::wstring> Definition"));
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<int>* variable,
                    MapModeCommands* map_mode) {
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
  LOG(INFO) << "Command: " << command;
  map_mode->Add(L"v" + variable->key(),
                ValueOrDie(NewCppCommand(editor_state,
                                         editor_state.environment(), command),
                           L"ToggleVariable<int> definition"));
}

template <typename T>
void RegisterVariableKeys(EditorState& editor_state, EdgeStruct<T>* edge_struct,
                          VariableLocation variable_location,
                          MapModeCommands* map_mode) {
  std::vector<std::wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const std::wstring& name : variable_names) {
    auto variable = edge_struct->find_variable(name);
    if (!variable->key().empty()) {
      ToggleVariable(editor_state, variable_location, variable, map_mode);
    }
  }
}
}  // namespace

using std::map;
using std::unique_ptr;

std::unique_ptr<MapModeCommands> NewCommandMode(EditorState& editor_state) {
  auto commands = std::make_unique<MapModeCommands>(editor_state);
  commands->Add(L"aq", NewQuitCommand(editor_state, 0));
  commands->Add(L"aQ", NewQuitCommand(editor_state, 1));
  commands->Add(L"av", NewSetVariableCommand(editor_state));
  commands->Add(L"ac", NewRunCppFileCommand(editor_state));
  commands->Add(L"aC",
                NewRunCppCommand(editor_state, CppCommandMode::kLiteral));
  commands->Add(L":", NewRunCppCommand(editor_state, CppCommandMode::kShell));
  commands->Add(L"a.", NewOpenDirectoryCommand(editor_state));
  commands->Add(L"ao", NewOpenFileCommand(editor_state));
  commands->Add(
      L"aF",
      NewLinePromptCommand(
          editor_state, L"forks a command for each line in the current buffer",
          [&editor_state] {
            return PromptOptions{
                .editor_state = editor_state,
                .prompt = L"...$ ",
                .history_file = HistoryFileCommands(),
                .handler = std::bind_front(RunMultipleCommandsHandler,
                                           std::ref(editor_state))};
          }));

  commands->Add(L"af", NewForkCommand(editor_state));

  commands->Add(L"N", NewNavigationBufferCommand(editor_state));
  commands->Add(L"i", MakeNonNullUnique<EnterInsertModeCommand>(editor_state,
                                                                std::nullopt));
  commands->Add(L"I",
                MakeNonNullUnique<EnterInsertModeCommand>(editor_state, [] {
                  Modifiers output;
                  output.insertion = Modifiers::ModifyMode::kOverwrite;
                  return output;
                }()));

  commands->Add(L"f", operation::NewTopLevelCommand(
                          L"find",
                          L"reaches the next occurrence of a specific "
                          L"character in the current line",
                          operation::TopCommand(), editor_state,
                          {operation::CommandReachChar{}}));
  commands->Add(
      L"F",
      operation::NewTopLevelCommand(
          L"find",
          L"reaches the previous occurrence of a specific "
          L"character in the current line",
          operation::TopCommand(), editor_state,
          {operation::CommandReachChar{
              .repetitions = operation::CommandArgumentRepetitions(-1)}}));

  commands->Add(L"r", operation::NewTopLevelCommand(
                          L"reach", L"starts a new reach command",
                          operation::TopCommand(), editor_state, {}));

  commands->Add(L"R",
                MakeNonNullUnique<InsertionModifierCommand>(editor_state));

  commands->Add(L"/", NewSearchCommand(editor_state));
  commands->Add(L"g", NewGotoCommand(editor_state));

  commands->Add(L"W", MakeNonNullUnique<SetStructureCommand>(
                          editor_state, StructureSymbol()));
  commands->Add(L"w", MakeNonNullUnique<SetStructureCommand>(editor_state,
                                                             StructureWord()));
  commands->Add(L"E", MakeNonNullUnique<SetStructureCommand>(editor_state,
                                                             StructurePage()));
  commands->Add(L"c", MakeNonNullUnique<SetStructureCommand>(
                          editor_state, StructureCursor()));
  commands->Add(L"B", MakeNonNullUnique<SetStructureCommand>(
                          editor_state, StructureBuffer()));
  commands->Add(L"!", MakeNonNullUnique<SetStructureCommand>(editor_state,
                                                             StructureMark()));
  commands->Add(L"t", MakeNonNullUnique<SetStructureCommand>(editor_state,
                                                             StructureTree()));

  commands->Add(
      L"e", operation::NewTopLevelCommand(
                L"delete", L"starts a new delete command",
                operation::TopCommand{
                    .post_transformation_behavior = transformation::Stack::
                        PostTransformationBehavior::kDeleteRegion},
                editor_state,
                {operation::CommandReach{
                    .repetitions = operation::CommandArgumentRepetitions(1)}}));
  commands->Add(L"p", NewPasteCommand(editor_state));

  commands->Add(L"u",
                MakeNonNullUnique<UndoCommand>(editor_state, std::nullopt));
  commands->Add(L"U", MakeNonNullUnique<UndoCommand>(editor_state,
                                                     Direction::kBackwards));
  commands->Add(L"\n", MakeNonNullUnique<ActivateLink>(editor_state));

  commands->Add(L"b",
                MakeNonNullUnique<GotoPreviousPositionCommand>(editor_state));
  commands->Add(L"n", NewNavigateCommand(editor_state));

  commands->Add(
      L"j", operation::NewTopLevelCommand(
                L"down", L"moves down one line", operation::TopCommand(),
                editor_state,
                {operation::CommandReachLine{
                    .repetitions = operation::CommandArgumentRepetitions(1)}}));
  commands->Add(
      L"k",
      operation::NewTopLevelCommand(
          L"up", L"moves up one line", operation::TopCommand(), editor_state,
          {operation::CommandReachLine{
              .repetitions = operation::CommandArgumentRepetitions(-1)}}));

  // commands->Add(L"j", std::make_unique<LineDown>());
  // commands->Add(L"k", std::make_unique<LineUp>());
  // commands->Add(L"l", std::make_unique<MoveForwards>(Direction::kForwards));
  // commands->Add(L"h", std::make_unique<MoveForwards>(Direction::kBackwards));
  commands->Add(
      L"l", operation::NewTopLevelCommand(
                L"right", L"moves right one position", operation::TopCommand(),
                editor_state,
                {operation::CommandReach{
                    .repetitions = operation::CommandArgumentRepetitions(1)}}));
  commands->Add(
      L"h",
      operation::NewTopLevelCommand(
          L"left", L"moves left one position", operation::TopCommand(),
          editor_state,
          {operation::CommandReach{
              .repetitions = operation::CommandArgumentRepetitions(-1)}}));

  commands->Add(
      L"H", operation::NewTopLevelCommand(
                L"home", L"moves to the beginning of the current line",
                operation::TopCommand(), editor_state,
                {operation::CommandReachBegin{
                    .structure = StructureChar(),
                    .repetitions = operation::CommandArgumentRepetitions(1)}}));
  commands->Add(L"L",
                operation::NewTopLevelCommand(
                    L"end", L"moves to the end of the current line",
                    operation::TopCommand(), editor_state,
                    {operation::CommandReachBegin{
                        .structure = StructureChar(),
                        .repetitions = operation::CommandArgumentRepetitions(1),
                        .direction = Direction::kBackwards}}));
  commands->Add(
      L"K", operation::NewTopLevelCommand(
                L"file-home", L"moves to the beginning of the current file",
                operation::TopCommand(), editor_state,
                {operation::CommandReachBegin{
                    .structure = StructureLine(),
                    .repetitions = operation::CommandArgumentRepetitions(1)}}));
  commands->Add(L"J",
                operation::NewTopLevelCommand(
                    L"file-end", L"moves to the end of the current line",
                    operation::TopCommand(), editor_state,
                    {operation::CommandReachBegin{
                        .structure = StructureLine(),
                        .repetitions = operation::CommandArgumentRepetitions(1),
                        .direction = Direction::kBackwards}}));
  commands->Add(L"~",
                NewCommandWithModifiers(
                    [](const Modifiers&) { return L"🔠🔡"; },
                    L"Switches the case of the current character.", Modifiers(),
                    [transformation =
                         NonNull<std::shared_ptr<SwitchCaseTransformation>>()](
                        Modifiers modifiers) {
                      return transformation::ModifiersAndComposite{
                          std::move(modifiers), transformation};
                    },
                    editor_state));

  commands->Add(L"%", MakeNonNullUnique<TreeNavigateCommand>(editor_state));
  commands->Add(L"sr", NewRecordCommand(editor_state));
  commands->Add(L"\t", NewFindCompletionCommand(editor_state));

  RegisterVariableKeys(editor_state, editor_variables::BoolStruct(),
                       VariableLocation::kEditor, commands.get());
  RegisterVariableKeys(editor_state, editor_variables::IntStruct(),
                       VariableLocation::kEditor, commands.get());
  RegisterVariableKeys(editor_state, buffer_variables::BoolStruct(),
                       VariableLocation::kBuffer, commands.get());
  RegisterVariableKeys(editor_state, buffer_variables::StringStruct(),
                       VariableLocation::kBuffer, commands.get());
  RegisterVariableKeys(editor_state, buffer_variables::IntStruct(),
                       VariableLocation::kBuffer, commands.get());

  commands->Add({Terminal::ESCAPE},
                MakeNonNullUnique<ResetStateCommand>(editor_state));

  commands->Add({Terminal::CTRL_L},
                MakeNonNullUnique<HardRedrawCommand>(editor_state));
  commands->Add(L"*", MakeNonNullUnique<SetStrengthCommand>(editor_state));
  commands->Add(L"0", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"1", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"2", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"3", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"4", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"5", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"6", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"7", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"8", MakeNonNullUnique<NumberMode>(editor_state));
  commands->Add(L"9", MakeNonNullUnique<NumberMode>(editor_state));

  commands->Add({Terminal::DOWN_ARROW},
                MakeNonNullUnique<LineDown>(editor_state));
  commands->Add({Terminal::UP_ARROW}, MakeNonNullUnique<LineUp>(editor_state));
  commands->Add(
      {Terminal::LEFT_ARROW},
      MakeNonNullUnique<MoveForwards>(editor_state, Direction::kBackwards));
  commands->Add(
      {Terminal::RIGHT_ARROW},
      MakeNonNullUnique<MoveForwards>(editor_state, Direction::kForwards));
  commands->Add(
      {Terminal::PAGE_DOWN},
      operation::NewTopLevelCommand(
          L"page_down", L"moves down one page", operation::TopCommand(),
          editor_state,
          {operation::CommandReachPage{
              .repetitions = operation::CommandArgumentRepetitions(1)}}));
  commands->Add(
      {Terminal::PAGE_UP},
      operation::NewTopLevelCommand(
          L"page_up", L"moves up one page", operation::TopCommand(),
          editor_state,
          {operation::CommandReachPage{
              .repetitions = operation::CommandArgumentRepetitions(-1)}}));
  return commands;
}

}  // namespace afc::editor
