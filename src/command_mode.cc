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
#include "src/insert_mode.h"
#include "src/language/wstring.h"
#include "src/lazy_string_append.h"
#include "src/line_column.h"
#include "src/line_prompt_mode.h"
#include "src/list_buffers_command.h"
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
#include "src/substring.h"
#include "src/terminal.h"
#include "src/time.h"
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

namespace afc {
namespace editor {
namespace {
using std::advance;
using std::ceil;
using std::make_pair;

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  Paste(EditorState& editor_state) : editor_state_(editor_state) {}

  wstring Description() const override {
    return L"pastes the last deleted text";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) {
    auto it = editor_state_.buffers()->find(BufferName::PasteBuffer());
    if (it == editor_state_.buffers()->end()) {
      const static wstring errors[] = {
          L"No text to paste.",
          L"Try deleting something first.",
          L"You can't paste what you haven't deleted.",
          L"First delete; then paste.",
          L"I have nothing to paste.",
          L"The paste buffer is empty.",
          L"There's nothing to paste.",
          L"Nope.",
          L"Let's see, is there's something to paste? Nope.",
          L"The paste buffer is desolate.",
          L"Paste what?",
          L"I'm sorry, Dave, I'm afraid I can't do that.",
          L"",
      };
      static int current_message = 0;
      editor_state_.status().SetWarningText(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    std::shared_ptr<OpenBuffer> paste_buffer = it->second;
    editor_state_
        .ForEachActiveBuffer([&editor_state = editor_state_,
                              paste_buffer](OpenBuffer& buffer) {
          if (paste_buffer.get() == &buffer) {
            const static wstring errors[] = {
                L"You shall not paste into the paste buffer.",
                L"Nope.",
                L"Bad things would happen if you pasted into the buffer.",
                L"There could be endless loops if you pasted into this "
                L"buffer.",
                L"This is not supported.",
                L"Go to a different buffer first?",
                L"The paste buffer is not for pasting into.",
                L"This editor is too important for me to allow you to "
                L"jeopardize it.",
                L"",
            };
            static int current_message = 0;
            buffer.status().SetWarningText(errors[current_message++]);
            if (errors[current_message].empty()) {
              current_message = 0;
            }
            return futures::Past(EmptyValue());
          }
          if (buffer.fd() != nullptr) {
            string text = ToByteString(paste_buffer->ToString());
            for (size_t i = 0; i < editor_state.repetitions(); i++) {
              if (write(buffer.fd()->fd().read(), text.c_str(), text.size()) ==
                  -1) {
                buffer.status().SetWarningText(L"Unable to paste.");
                break;
              }
            }
            return futures::Past(EmptyValue());
          }
          buffer.CheckPosition();
          buffer.MaybeAdjustPositionCol();
          return buffer.ApplyToCursors(transformation::Insert{
              .contents_to_insert = paste_buffer->contents().copy(),
              .modifiers = {.insertion = editor_state.modifiers().insertion,
                            .repetitions = editor_state.repetitions()}});
        })
        .Transform([&editor_state = editor_state_](EmptyValue) {
          editor_state.ResetInsertionModifier();
          editor_state.ResetRepetitions();
          return EmptyValue();
        });
  }

 private:
  EditorState& editor_state_;
};

class UndoCommand : public Command {
 public:
  UndoCommand(EditorState& editor_state, std::optional<Direction> direction)
      : editor_state_(editor_state), direction_(direction) {}

  wstring Description() const override {
    if (direction_.value_or(Direction::kForwards) == Direction::kBackwards) {
      return L"re-does the last change to the current buffer";
    }
    return L"un-does the last change to the current buffer";
  }

  wstring Category() const override { return L"Edit"; }

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

  wstring Description() const override {
    return L"go back to previous position";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t) {
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
      const LineColumn current_position =
          editor_state_.current_buffer()->position();
      if (it != editor_state_.buffers()->end() &&
          (pos.buffer_name != editor_state_.current_buffer()->name() ||
           ((editor_state_.structure() == StructureLine() ||
             editor_state_.structure() == StructureWord() ||
             editor_state_.structure() == StructureSymbol() ||
             editor_state_.structure() == StructureChar()) &&
            pos.position.line != current_position.line) ||
           (editor_state_.structure() == StructureChar() &&
            pos.position.column != current_position.column))) {
        LOG(INFO) << "Jumping to position: "
                  << it->second->Read(buffer_variables::name) << " "
                  << pos.position;
        editor_state_.set_current_buffer(it->second,
                                         CommandArgumentModeApplyMode::kFinal);
        it->second->set_position(pos.position);
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

  wstring Category() const override { return L"Navigate"; }

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
  wstring Category() const override { return L"Navigate"; }
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
      auto buffer = editor_state.current_buffer();
      if (buffer == nullptr) {
        return;
      }
      switch (editor_state.direction()) {
        case Direction::kBackwards:
          if (buffer->tree_depth() > 0) {
            buffer->set_tree_depth(buffer->tree_depth() - 1);
          }
          break;
        case Direction::kForwards: {
          auto root = buffer->parse_tree();
          const ParseTree* tree = buffer->current_tree(root.get());
          if (!tree->children().empty()) {
            buffer->set_tree_depth(buffer->tree_depth() + 1);
          }
          break;
        }
      }
      buffer->ResetMode();
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

  wstring Description() const override { return L"enters insert mode"; }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t) {
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

  wstring Description() const override {
    return L"activates replace modifier (overwrites text on insertions)";
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) {
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

  wstring Description() const override {
    return L"reverses the direction of the next command";
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) {
    editor_state_.set_direction(ReverseDirection(editor_state_.direction()));
  }

 private:
  EditorState& editor_state_;
};

class SetStructureCommand : public Command {
 public:
  SetStructureCommand(EditorState& editor_state, Structure* structure)
      : editor_state_(editor_state), structure_(structure) {}

  wstring Description() const override {
    return L"sets the structure: " + structure_->ToString();
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) {
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
  const wstring description_;
};

class SetStrengthCommand : public Command {
 public:
  SetStrengthCommand(EditorState& editor_state) : editor_state_(editor_state) {}

  wstring Description() const override { return L"Toggles the strength."; }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t) {
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
  NumberMode(EditorState& editor_state, const wstring& description)
      : editor_state_(editor_state), description_(description) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t c) {
    editor_state_.set_keyboard_redirect(NewRepeatMode(
        editor_state_, [&editor_state = editor_state_](int number) {
          editor_state.set_repetitions(number);
        }));
    editor_state_.ProcessInput(c);
  }

 private:
  EditorState& editor_state_;
  const wstring description_;
};

class ActivateLink : public Command {
 public:
  ActivateLink(EditorState& editor_state) : editor_state_(editor_state) {}
  wstring Description() const override {
    return L"activates the current link (if any)";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t) {
    auto buffer = editor_state_.current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (buffer->current_line() == nullptr) {
      return;
    }

    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr && target != buffer) {
      LOG(INFO) << "Visiting buffer: " << target->Read(buffer_variables::name);
      editor_state_.status().Reset();
      buffer->status().Reset();
      editor_state_.set_current_buffer(target,
                                       CommandArgumentModeApplyMode::kFinal);
      auto target_position = buffer->current_line()->environment()->Lookup(
          Environment::Namespace(), L"buffer_position",
          vm::VMTypeMapper<LineColumn>::vmtype);
      if (target_position != nullptr &&
          target_position->type == VMType::ObjectType(L"LineColumn")) {
        target->set_position(
            *static_cast<LineColumn*>(target_position->user_value.get()));
      }
      editor_state_.PushCurrentPosition();
      buffer->ResetMode();
      target->ResetMode();
      return;
    }

    buffer->MaybeAdjustPositionCol();
    buffer
        ->OpenBufferForCurrentPosition(
            OpenBuffer::RemoteURLBehavior::kLaunchBrowser)
        .Transform([&editor_state =
                        editor_state_](std::shared_ptr<OpenBuffer> target) {
          if (target != nullptr) {
            if (std::wstring path = target->Read(buffer_variables::path);
                !path.empty())
              AddLineToHistory(editor_state, HistoryFileFiles(),
                               NewLazyString(path));
            editor_state.AddBuffer(target, BuffersList::AddBufferType::kVisit);
          }
          return Success();
        });
  }

 private:
  EditorState& editor_state_;
};

class ResetStateCommand : public Command {
 public:
  ResetStateCommand(EditorState& editor_state) : editor_state_(editor_state) {}
  wstring Description() const override {
    return L"Resets the state of the editor.";
  }
  wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t) {
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
  wstring Description() const override { return L"Redraws the screen"; }
  wstring Category() const override { return L"View"; }

  void ProcessInput(wint_t) {
    editor_state_.set_screen_needs_hard_redraw(true);
  }

 private:
  EditorState& editor_state_;
};

class TreeNavigateCommand : public Command {
 public:
  TreeNavigateCommand(EditorState& editor_state)
      : editor_state_(editor_state) {}
  wstring Description() const override {
    return L"Navigates to the start/end of the current children of the "
           L"syntax tree";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t) {
    static const auto transformation =
        new std::shared_ptr<CompositeTransformation>(
            std::make_shared<TreeNavigate>());
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
  map_mode->Add(
      L"v" + variable->key(),
      NewCppCommand(editor_state, editor_state.environment(), command));
}

void ToggleVariable(EditorState& editor_state,
                    VariableLocation variable_location,
                    const EdgeVariable<wstring>* variable,
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
  map_mode->Add(
      L"v" + variable->key(),
      NewCppCommand(editor_state, editor_state.environment(), command));
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
  map_mode->Add(
      L"v" + variable->key(),
      NewCppCommand(editor_state, editor_state.environment(), command));
}

template <typename T>
void RegisterVariableKeys(EditorState& editor_state, EdgeStruct<T>* edge_struct,
                          VariableLocation variable_location,
                          MapModeCommands* map_mode) {
  std::vector<std::wstring> variable_names;
  edge_struct->RegisterVariableNames(&variable_names);
  for (const wstring& name : variable_names) {
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
  commands->Add(L"aL", NewListBuffersCommand(editor_state));
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
                .handler = [&editor_state](std::wstring input) {
                  return RunMultipleCommandsHandler(input, editor_state);
                }};
          }));

  commands->Add(L"af", NewForkCommand(editor_state));

  commands->Add(L"N", NewNavigationBufferCommand(editor_state));
  commands->Add(L"i", std::make_unique<EnterInsertModeCommand>(editor_state,
                                                               std::nullopt));
  commands->Add(L"I",
                std::make_unique<EnterInsertModeCommand>(editor_state, [] {
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

  commands->Add(L"R", std::make_unique<InsertionModifierCommand>(editor_state));

  commands->Add(L"/", NewSearchCommand(editor_state));
  commands->Add(L"g", NewGotoCommand(editor_state));

  commands->Add(L"W", std::make_unique<SetStructureCommand>(editor_state,
                                                            StructureSymbol()));
  commands->Add(L"w", std::make_unique<SetStructureCommand>(editor_state,
                                                            StructureWord()));
  commands->Add(L"E", std::make_unique<SetStructureCommand>(editor_state,
                                                            StructurePage()));
  commands->Add(L"c", std::make_unique<SetStructureCommand>(editor_state,
                                                            StructureCursor()));
  commands->Add(L"B", std::make_unique<SetStructureCommand>(editor_state,
                                                            StructureBuffer()));
  commands->Add(L"!", std::make_unique<SetStructureCommand>(editor_state,
                                                            StructureMark()));
  commands->Add(L"t", std::make_unique<SetStructureCommand>(editor_state,
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
  commands->Add(L"p", std::make_unique<Paste>(editor_state));

  commands->Add(L"u",
                std::make_unique<UndoCommand>(editor_state, std::nullopt));
  commands->Add(
      L"U", std::make_unique<UndoCommand>(editor_state, Direction::kBackwards));
  commands->Add(L"\n", std::make_unique<ActivateLink>(editor_state));

  commands->Add(L"b",
                std::make_unique<GotoPreviousPositionCommand>(editor_state));
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
  commands->Add(
      L"~", NewCommandWithModifiers(
                [](const Modifiers&) { return L"🔠🔡"; },
                L"Switches the case of the current character.", Modifiers(),
                [transformation = std::make_shared<SwitchCaseTransformation>()](
                    Modifiers modifiers) {
                  return transformation::ModifiersAndComposite{
                      std::move(modifiers), transformation};
                },
                editor_state));

  commands->Add(L"%", std::make_unique<TreeNavigateCommand>(editor_state));
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
                std::make_unique<ResetStateCommand>(editor_state));

  commands->Add({Terminal::CTRL_L},
                std::make_unique<HardRedrawCommand>(editor_state));
  commands->Add(L"*", std::make_unique<SetStrengthCommand>(editor_state));
  commands->Add(L"0", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"1", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"2", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"3", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"4", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"5", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"6", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"7", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"8", std::make_unique<NumberMode>(editor_state));
  commands->Add(L"9", std::make_unique<NumberMode>(editor_state));

  commands->Add({Terminal::DOWN_ARROW},
                std::make_unique<LineDown>(editor_state));
  commands->Add({Terminal::UP_ARROW}, std::make_unique<LineUp>(editor_state));
  commands->Add(
      {Terminal::LEFT_ARROW},
      std::make_unique<MoveForwards>(editor_state, Direction::kBackwards));
  commands->Add(
      {Terminal::RIGHT_ARROW},
      std::make_unique<MoveForwards>(editor_state, Direction::kForwards));
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

}  // namespace editor
}  // namespace afc
