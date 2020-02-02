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

#include "src/buffer_variables.h"
#include "src/char_buffer.h"
#include "src/close_buffer_command.h"
#include "src/command.h"
#include "src/cpp_command.h"
#include "src/delete_mode.h"
#include "src/dirname.h"
#include "src/file_descriptor_reader.h"
#include "src/file_link_mode.h"
#include "src/find_mode.h"
#include "src/goto_command.h"
#include "src/insert_mode.h"
#include "src/lazy_string_append.h"
#include "src/line_column.h"
#include "src/line_prompt_mode.h"
#include "src/list_buffers_command.h"
#include "src/map_mode.h"
#include "src/navigate_command.h"
#include "src/navigation_buffer.h"
#include "src/open_directory_command.h"
#include "src/open_file_command.h"
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
#include "src/send_end_of_file_command.h"
#include "src/set_variable_command.h"
#include "src/substring.h"
#include "src/terminal.h"
#include "src/transformation.h"
#include "src/transformation/delete.h"
#include "src/transformation/insert.h"
#include "src/transformation/move.h"
#include "src/transformation/set_position.h"
#include "src/transformation/stack.h"
#include "src/transformation/switch_case.h"
#include "src/transformation/tree_navigate.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
using std::advance;
using std::ceil;
using std::make_pair;

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  wstring Description() const override {
    return L"pastes the last deleted text";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto it = editor_state->buffers()->find(OpenBuffer::kPasteBuffer);
    if (it == editor_state->buffers()->end()) {
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
      editor_state->status()->SetWarningText(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    std::shared_ptr<OpenBuffer> paste_buffer = it->second;
    futures::ImmediateTransform(
        editor_state->ForEachActiveBuffer(
            [editor_state,
             paste_buffer](const std::shared_ptr<OpenBuffer>& buffer) {
              if (paste_buffer == buffer) {
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
                buffer->status()->SetWarningText(errors[current_message++]);
                if (errors[current_message].empty()) {
                  current_message = 0;
                }
                return futures::Past(true);
              }
              if (buffer->fd() != nullptr) {
                string text = ToByteString(paste_buffer->ToString());
                for (size_t i = 0; i < editor_state->repetitions(); i++) {
                  if (write(buffer->fd()->fd(), text.c_str(), text.size()) ==
                      -1) {
                    buffer->status()->SetWarningText(L"Unable to paste.");
                    break;
                  }
                }
                return futures::Past(true);
              }
              buffer->CheckPosition();
              buffer->MaybeAdjustPositionCol();
              InsertOptions insert_options;
              insert_options.buffer_to_insert = paste_buffer;
              insert_options.modifiers.insertion =
                  editor_state->modifiers().insertion;
              insert_options.modifiers.repetitions =
                  editor_state->repetitions();
              return futures::Transform(
                  buffer->ApplyToCursors(
                      NewInsertBufferTransformation(std::move(insert_options))),
                  futures::Past(true));
            }),
        [editor_state](bool) {
          editor_state->ResetInsertionModifier();
          editor_state->ResetRepetitions();
          return futures::Past(true);
        });
  }
};

class UndoCommand : public Command {
 public:
  UndoCommand(std::optional<Direction> direction) : direction_(direction) {}

  wstring Description() const override {
    if (direction_.value_or(FORWARDS) == BACKWARDS) {
      return L"re-does the last change to the current buffer";
    }
    return L"un-does the last change to the current buffer";
  }

  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    if (direction_.has_value()) {
      editor_state->set_direction(direction_.value());
    }
    futures::ImmediateTransform(
        editor_state->ForEachActiveBuffer(
            [](const std::shared_ptr<OpenBuffer>& buffer) {
              return buffer->Undo(OpenBuffer::UndoMode::kLoop);
            }),
        [editor_state](bool) {
          editor_state->ResetRepetitions();
          editor_state->ResetDirection();
          return true;
        });
  }

 private:
  const std::optional<Direction> direction_;
};

class GotoPreviousPositionCommand : public Command {
 public:
  wstring Description() const override {
    return L"go back to previous position";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    Go(editor_state);
    editor_state->ResetDirection();
    editor_state->ResetRepetitions();
    editor_state->ResetStructure();
  }

  static void Go(EditorState* editor_state) {
    if (!editor_state->HasPositionsInStack()) {
      LOG(INFO) << "Editor doesn't have positions in stack.";
      return;
    }
    while (editor_state->repetitions() > 0) {
      if (!editor_state->MovePositionsStack(editor_state->direction())) {
        LOG(INFO) << "Editor failed to move in positions stack.";
        return;
      }
      const BufferPosition pos = editor_state->ReadPositionsStack();
      auto it = editor_state->buffers()->find(pos.buffer_name);
      const LineColumn current_position =
          editor_state->current_buffer()->position();
      if (it != editor_state->buffers()->end() &&
          (pos.buffer_name !=
               editor_state->current_buffer()->Read(buffer_variables::name) ||
           ((editor_state->structure() == StructureLine() ||
             editor_state->structure() == StructureWord() ||
             editor_state->structure() == StructureSymbol() ||
             editor_state->structure() == StructureChar()) &&
            pos.position.line != current_position.line) ||
           (editor_state->structure() == StructureChar() &&
            pos.position.column != current_position.column))) {
        LOG(INFO) << "Jumping to position: "
                  << it->second->Read(buffer_variables::name) << " "
                  << pos.position;
        editor_state->set_current_buffer(it->second);
        it->second->set_position(pos.position);
        editor_state->set_repetitions(editor_state->repetitions() - 1);
      }
    }
  }
};

class LineUp : public Command {
 public:
  wstring Description() const override;
  wstring Category() const override { return L"Navigate"; }
  static void Move(int c, EditorState* editor_state, Structure* structure);
  void ProcessInput(wint_t c, EditorState* editor_state) override;
};

class LineDown : public Command {
 public:
  wstring Description() const override;
  wstring Category() const override { return L"Navigate"; }
  static void Move(int c, EditorState* editor_state, Structure* structure);
  void ProcessInput(wint_t c, EditorState* editor_state) override;
};

class PageUp : public Command {
 public:
  wstring Description() const override;
  wstring Category() const override { return L"Navigate"; }
  static void Move(int c, EditorState* editor_state);
  void ProcessInput(wint_t c, EditorState* editor_state) override;
};

class PageDown : public Command {
 public:
  wstring Description() const override;
  wstring Category() const override { return L"Navigate"; }
  void ProcessInput(wint_t c, EditorState* editor_state) override;
};

class MoveForwards : public Command {
 public:
  wstring Description() const override;
  wstring Category() const override { return L"Navigate"; }
  void ProcessInput(wint_t c, EditorState* editor_state) override;
  static void Move(int c, EditorState* editor_state);
};

class MoveBackwards : public Command {
 public:
  wstring Description() const override;
  wstring Category() const override { return L"Navigate"; }
  void ProcessInput(wint_t c, EditorState* editor_state) override;
  static void Move(int c, EditorState* editor_state);
};

wstring LineUp::Description() const { return L"moves up one line"; }

/* static */ void LineUp::Move(int c, EditorState* editor_state,
                               Structure* structure) {
  if (editor_state->direction() == BACKWARDS || structure == StructureTree()) {
    editor_state->set_direction(ReverseDirection(editor_state->direction()));
    LineDown::Move(c, editor_state, structure);
    return;
  }
  // TODO: Move to Structure.
  if (structure == StructureChar()) {
    editor_state->set_structure(StructureLine());
    MoveBackwards::Move(c, editor_state);
  } else if (structure == StructureWord() || structure == StructureSymbol()) {
    // Move in whole pages.
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    auto view_size = buffer->viewers()->view_size();
    auto lines = view_size.has_value() ? view_size->line : LineNumberDelta(1);
    editor_state->set_repetitions(
        editor_state->repetitions() *
        (lines.line_delta > 2 ? lines.line_delta - 2 : 3));
    VLOG(6) << "Word Up: Repetitions: " << editor_state->repetitions();
    Move(c, editor_state, StructureChar());
  } else if (structure == StructureTree()) {
    CHECK(false);  // Handled above.
  } else {
    editor_state->MoveBufferBackwards(editor_state->repetitions());
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
  editor_state->ResetDirection();
}

void LineUp::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state, editor_state->structure());
}

wstring LineDown::Description() const { return L"moves down one line"; }

/* static */ void LineDown::Move(int c, EditorState* editor_state,
                                 Structure* structure) {
  if (editor_state->direction() == BACKWARDS && structure != StructureTree()) {
    editor_state->set_direction(FORWARDS);
    LineUp::Move(c, editor_state, structure);
    return;
  }
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  // TODO: Move to Structure.
  if (structure == StructureChar()) {
    editor_state->set_structure(StructureLine());
    MoveForwards::Move(c, editor_state);
  } else if (structure == StructureWord() || structure == StructureSymbol()) {
    // Move in whole pages.
    auto view_size = buffer->viewers()->view_size();
    auto lines = view_size.has_value() ? view_size->line : LineNumberDelta(1);
    editor_state->set_repetitions(
        editor_state->repetitions() *
        (lines.line_delta > 2 ? lines.line_delta - 2 : 3));
    VLOG(6) << "Word Down: Repetitions: " << editor_state->repetitions();
    Move(c, editor_state, StructureChar());
  } else if (structure == StructureTree()) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (editor_state->direction() == BACKWARDS) {
      if (buffer->tree_depth() > 0) {
        buffer->set_tree_depth(buffer->tree_depth() - 1);
      }
    } else if (editor_state->direction() == FORWARDS) {
      auto root = buffer->parse_tree();
      const ParseTree* tree = buffer->current_tree(root.get());
      if (!tree->children().empty()) {
        buffer->set_tree_depth(buffer->tree_depth() + 1);
      }
    } else {
      CHECK(false) << "Invalid direction: " << editor_state->direction();
    }
    buffer->ResetMode();
    editor_state->ResetDirection();
    editor_state->ResetStructure();
  } else {
    editor_state->MoveBufferForwards(editor_state->repetitions());
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
  editor_state->ResetDirection();
}

void LineDown::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state, editor_state->structure());
}

wstring PageUp::Description() const { return L"moves up one page"; }

void PageUp::ProcessInput(wint_t c, EditorState* editor_state) {
  editor_state->ResetStructure();
  LineUp::Move(c, editor_state, StructureWord());
}

wstring PageDown::Description() const { return L"moves down one page"; }

void PageDown::ProcessInput(wint_t c, EditorState* editor_state) {
  editor_state->ResetStructure();
  LineDown::Move(c, editor_state, StructureWord());
}

wstring MoveForwards::Description() const { return L"moves forwards"; }

void MoveForwards::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state);
}

/* static */ void MoveForwards::Move(int, EditorState* editor_state) {
  editor_state->ForEachActiveBuffer(
      [modifiers = editor_state->modifiers()](
          const std::shared_ptr<OpenBuffer>& buffer) {
        return buffer->ApplyToCursors(NewMoveTransformation(modifiers));
      });
  editor_state->ResetRepetitions();
  editor_state->ResetStructure();
  editor_state->ResetDirection();
}

wstring MoveBackwards::Description() const { return L"moves backwards"; }

void MoveBackwards::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state);
}

/* static */ void MoveBackwards::Move(int c, EditorState* editor_state) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    MoveForwards::Move(c, editor_state);
    return;
  }
  editor_state->set_direction(ReverseDirection(editor_state->direction()));
  MoveForwards::Move(c, editor_state);
  return;
}

class EnterInsertModeCommand : public Command {
 public:
  EnterInsertModeCommand(std::optional<Modifiers> modifiers)
      : modifiers_(std::move(modifiers)) {}

  wstring Description() const override { return L"enters insert mode"; }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (modifiers_.has_value()) {
      editor_state->set_modifiers(modifiers_.value());
    }
    InsertModeOptions options;
    options.editor_state = editor_state;
    EnterInsertMode(options);
  }

 private:
  const std::optional<Modifiers> modifiers_;
};

class InsertionModifierCommand : public Command {
 public:
  wstring Description() const override {
    return L"activates replace modifier (overwrites text on insertions)";
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->insertion_modifier() == Modifiers::ModifyMode::kShift) {
      editor_state->set_insertion_modifier(Modifiers::ModifyMode::kOverwrite);
    } else if (editor_state->default_insertion_modifier() ==
               Modifiers::ModifyMode::kShift) {
      editor_state->set_default_insertion_modifier(
          Modifiers::ModifyMode::kOverwrite);
    } else {
      editor_state->set_default_insertion_modifier(
          Modifiers::ModifyMode::kShift);
      editor_state->ResetInsertionModifier();
    }
  }
};

class ReverseDirectionCommand : public Command {
 public:
  wstring Description() const override {
    return L"reverses the direction of the next command";
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    VLOG(3) << "Setting reverse direction. [previous modifiers: "
            << editor_state->modifiers();
    if (editor_state->direction() == FORWARDS) {
      editor_state->set_direction(BACKWARDS);
    } else if (editor_state->default_direction() == FORWARDS) {
      editor_state->set_default_direction(BACKWARDS);
    } else {
      editor_state->set_default_direction(FORWARDS);
      editor_state->ResetDirection();
    }
    VLOG(5) << "After setting, modifiers: " << editor_state->modifiers();
  }
};

class SetStructureCommand : public Command {
 public:
  SetStructureCommand(Structure* structure) : structure_(structure) {}

  wstring Description() const override {
    return L"sets the structure: " + structure_->ToString();
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->structure() != structure_) {
      editor_state->set_structure(structure_);
      editor_state->set_sticky_structure(false);
    } else if (!editor_state->sticky_structure()) {
      editor_state->set_sticky_structure(true);
    } else {
      editor_state->set_structure(StructureChar());
      editor_state->set_sticky_structure(false);
    }
  }

 private:
  Structure* structure_;
  const wstring description_;
};

class SetStrengthCommand : public Command {
 public:
  wstring Description() const override { return L"Toggles the strength."; }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    Modifiers modifiers(editor_state->modifiers());
    switch (modifiers.strength) {
      case Modifiers::Strength::kNormal:
        modifiers.strength = Modifiers::Strength::kStrong;
        break;
      case Modifiers::Strength::kStrong:
        modifiers.strength = Modifiers::Strength::kNormal;
        break;
    }
    editor_state->set_modifiers(modifiers);
  }
};

class NumberMode : public Command {
 public:
  NumberMode() : description_(L"") {}

  NumberMode(const wstring& description) : description_(description) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    editor_state->set_keyboard_redirect(NewRepeatMode(
        [editor_state](int number) { editor_state->set_repetitions(number); }));
    editor_state->ProcessInput(c);
  }

 private:
  const wstring description_;
};

class ActivateLink : public Command {
 public:
  wstring Description() const override {
    return L"activates the current link (if any)";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    if (buffer->current_line() == nullptr) {
      return;
    }

    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr && target != buffer) {
      LOG(INFO) << "Visiting buffer: " << target->Read(buffer_variables::name);
      editor_state->status()->Reset();
      buffer->status()->Reset();
      editor_state->set_current_buffer(target);
      auto target_position = buffer->current_line()->environment()->Lookup(
          L"buffer_position", vm::VMTypeMapper<LineColumn>::vmtype);
      if (target_position != nullptr &&
          target_position->type == VMType::ObjectType(L"LineColumn")) {
        target->set_position(
            *static_cast<LineColumn*>(target_position->user_value.get()));
      }
      editor_state->PushCurrentPosition();
      buffer->ResetMode();
      target->ResetMode();
      return;
    }

    buffer->MaybeAdjustPositionCol();
    wstring line = buffer->current_line()->ToString();

    const wstring& path_characters =
        buffer->Read(buffer_variables::path_characters);

    // Scroll back to the first non-path character. If we're in a non-path
    // character, this is a no-op.
    size_t start = line.find_last_not_of(path_characters,
                                         buffer->current_position_col().column);
    if (start == line.npos) {
      start = 0;
    }

    // Scroll past any non-path characters. Typically this will just skip the
    // character we landed at in the block above. However, if we started in a
    // sequence of non-path characters, we skip them all.
    start = line.find_first_of(path_characters, start);
    if (start != line.npos) {
      line = line.substr(start);
    }

    size_t end = line.find_first_not_of(path_characters);
    if (end != line.npos) {
      line = line.substr(0, end);
    }

    if (line.empty()) {
      return;
    }

    OpenFileOptions options;
    options.editor_state = editor_state;
    options.path = line;
    options.ignore_if_not_found = true;

    options.initial_search_paths.clear();
    // Works if the current buffer is a directory listing:
    options.initial_search_paths.push_back(
        buffer->Read(buffer_variables::path));
    // And a fall-back for the current buffer being a file:
    options.initial_search_paths.push_back(
        Dirname(options.initial_search_paths[0]));
    LOG(INFO) << "Initial search path: " << options.initial_search_paths[0];

    OpenFile(options);
  }
};

class ResetStateCommand : public Command {
 public:
  wstring Description() const override {
    return L"Resets the state of the editor.";
  }
  wstring Category() const override { return L"Editor"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->status()->Reset();
    auto buffer = editor_state->current_buffer();
    if (buffer != nullptr) {
      buffer->status()->Reset();
    }
    editor_state->set_modifiers(Modifiers());
  }
};

class HardRedrawCommand : public Command {
 public:
  wstring Description() const override { return L"Redraws the screen"; }
  wstring Category() const override { return L"View"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->set_screen_needs_hard_redraw(true);
  }
};

std::unique_ptr<Transformation> ApplySwitchCaseCommand(EditorState*,
                                                       Modifiers modifiers) {
  if (modifiers.repetitions == 0) {
    modifiers.repetitions = 1;
  }
  return NewSwitchCaseTransformation(modifiers);
}

class TreeNavigateCommand : public Command {
 public:
  wstring Description() const override {
    return L"Navigates to the start/end of the current children of the "
           L"syntax tree";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->ForEachActiveBuffer(
        [](const std::shared_ptr<OpenBuffer>& buffer) {
          return buffer->ApplyToCursors(NewTreeNavigateTransformation());
        });
  }
};

enum class VariableLocation { kBuffer, kEditor };

void ToggleVariable(EditorState* editor_state,
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
                name + L"(!buffer." + name + L"()); buffer.SetStatus((buffer." +
                name + L"() ? \"🗸\" : \"⛶\") + \" " + name + L"\"); });";
      break;
    case VariableLocation::kEditor:
      command = L"// Variables: Toggle editor variable: " + name +
                L"\neditor.set_" + name + L"(!editor." + name +
                L"()); SetStatus((editor." + name +
                L"() ? \"🗸\" : \"⛶\") + \" " + name + L"\");";
      break;
  }
  LOG(INFO) << "Command: " << command;
  map_mode->Add(L"v" + variable->key(),
                NewCppCommand(editor_state->environment(), command));
}

void ToggleVariable(EditorState* editor_state,
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
                name + L"(repetitions());buffer.SetStatus(\"" + name +
                L" := \" + buffer." + name +
                L"().tostring()); }); set_repetitions(1);\n";
      break;
    case VariableLocation::kEditor:
      CHECK(false) << "Not implemented.";
      break;
  }
  LOG(INFO) << "Command: " << command;
  map_mode->Add(L"v" + variable->key(),
                NewCppCommand(editor_state->environment(), command));
}

template <typename T>
void RegisterVariableKeys(EditorState* editor_state, EdgeStruct<T>* edge_struct,
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

std::unique_ptr<MapModeCommands> NewCommandMode(EditorState* editor_state) {
  auto commands = std::make_unique<MapModeCommands>();
  commands->Add(L"aq", NewQuitCommand(0));
  commands->Add(L"aQ", NewQuitCommand(1));
  commands->Add(L"ad", NewCloseBufferCommand());
  commands->Add(L"av", NewSetVariableCommand(editor_state));
  commands->Add(L"ac", NewRunCppFileCommand());
  commands->Add(L"aC", NewRunCppCommand(CppCommandMode::kLiteral));
  commands->Add(L":", NewRunCppCommand(CppCommandMode::kShell));
  commands->Add(L"a.", NewOpenDirectoryCommand());
  commands->Add(L"aL", NewListBuffersCommand());
  commands->Add(L"ae", NewSendEndOfFileCommand());
  commands->Add(L"ao", NewOpenFileCommand());
  {
    PromptOptions options;
    options.editor_state = editor_state;
    options.prompt = L"...$ ";
    options.history_file = L"commands";
    options.handler = RunMultipleCommandsHandler;
    commands->Add(L"aF",
                  NewLinePromptCommand(
                      L"forks a command for each line in the current buffer",
                      [options](EditorState*) { return options; }));
  }
  commands->Add(L"af", NewForkCommand());

  commands->Add(L"N", NewNavigationBufferCommand());
  commands->Add(L"i", std::make_unique<EnterInsertModeCommand>(std::nullopt));
  commands->Add(L"I", std::make_unique<EnterInsertModeCommand>([] {
                  Modifiers output;
                  output.insertion = Modifiers::ModifyMode::kOverwrite;
                  return output;
                }()));
  commands->Add(L"f", NewFindModeCommand());
  commands->Add(L"r", std::make_unique<ReverseDirectionCommand>());
  commands->Add(L"R", std::make_unique<InsertionModifierCommand>());

  commands->Add(L"/", NewSearchCommand());
  commands->Add(L"g", NewGotoCommand());

  commands->Add(L"W", std::make_unique<SetStructureCommand>(StructureSymbol()));
  commands->Add(L"w", std::make_unique<SetStructureCommand>(StructureWord()));
  commands->Add(L"e", std::make_unique<SetStructureCommand>(StructureLine()));
  commands->Add(L"E", std::make_unique<SetStructureCommand>(StructurePage()));
  commands->Add(L"F", std::make_unique<SetStructureCommand>(StructureSearch()));
  commands->Add(L"c", std::make_unique<SetStructureCommand>(StructureCursor()));
  commands->Add(L"B", std::make_unique<SetStructureCommand>(StructureBuffer()));
  commands->Add(L"!", std::make_unique<SetStructureCommand>(StructureMark()));
  commands->Add(L"t", std::make_unique<SetStructureCommand>(StructureTree()));

  commands->Add(L"D", NewCommandWithModifiers(
                          L"✀ ", L"starts a new delete backwards command",
                          [] {
                            Modifiers output;
                            output.direction = BACKWARDS;
                            return output;
                          }(),
                          ApplyDeleteCommand));
  commands->Add(L"d",
                NewCommandWithModifiers(L"✀ ", L"starts a new delete command",
                                        Modifiers(), ApplyDeleteCommand));
  commands->Add(L"p", std::make_unique<Paste>());

  DeleteOptions copy_options;
  copy_options.modifiers.delete_behavior =
      Modifiers::DeleteBehavior::kDoNothing;
  commands->Add(L"u", std::make_unique<UndoCommand>(std::nullopt));
  commands->Add(L"U", std::make_unique<UndoCommand>(BACKWARDS));
  commands->Add(L"\n", std::make_unique<ActivateLink>());

  commands->Add(L"b", std::make_unique<GotoPreviousPositionCommand>());
  commands->Add(L"n", NewNavigateCommand());
  commands->Add(L"j", std::make_unique<LineDown>());
  commands->Add(L"k", std::make_unique<LineUp>());
  commands->Add(L"l", std::make_unique<MoveForwards>());
  commands->Add(L"h", std::make_unique<MoveBackwards>());

  commands->Add(L"~", NewCommandWithModifiers(
                          L"🔠🔡", L"Switches the case of the current character.",
                          Modifiers(), ApplySwitchCaseCommand));

  commands->Add(L"%", std::make_unique<TreeNavigateCommand>());
  commands->Add(L"sr", NewRecordCommand());
  commands->Add(L"\t", NewFindCompletionCommand());

  RegisterVariableKeys(editor_state, editor_variables::BoolStruct(),
                       VariableLocation::kEditor, commands.get());
  RegisterVariableKeys(editor_state, buffer_variables::BoolStruct(),
                       VariableLocation::kBuffer, commands.get());
  RegisterVariableKeys(editor_state, buffer_variables::IntStruct(),
                       VariableLocation::kBuffer, commands.get());

  commands->Add({Terminal::ESCAPE}, std::make_unique<ResetStateCommand>());

  commands->Add({Terminal::CTRL_L}, std::make_unique<HardRedrawCommand>());
  commands->Add(L"*", std::make_unique<SetStrengthCommand>());
  commands->Add(L"0", std::make_unique<NumberMode>());
  commands->Add(L"1", std::make_unique<NumberMode>());
  commands->Add(L"2", std::make_unique<NumberMode>());
  commands->Add(L"3", std::make_unique<NumberMode>());
  commands->Add(L"4", std::make_unique<NumberMode>());
  commands->Add(L"5", std::make_unique<NumberMode>());
  commands->Add(L"6", std::make_unique<NumberMode>());
  commands->Add(L"7", std::make_unique<NumberMode>());
  commands->Add(L"8", std::make_unique<NumberMode>());
  commands->Add(L"9", std::make_unique<NumberMode>());

  commands->Add({Terminal::DOWN_ARROW}, std::make_unique<LineDown>());
  commands->Add({Terminal::UP_ARROW}, std::make_unique<LineUp>());
  commands->Add({Terminal::LEFT_ARROW}, std::make_unique<MoveBackwards>());
  commands->Add({Terminal::RIGHT_ARROW}, std::make_unique<MoveForwards>());
  commands->Add({Terminal::PAGE_DOWN}, std::make_unique<PageDown>());
  commands->Add({Terminal::PAGE_UP}, std::make_unique<PageUp>());

  return commands;
}

}  // namespace editor
}  // namespace afc
