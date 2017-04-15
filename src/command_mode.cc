#include <algorithm>
#include <cmath>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <string>

#include <glog/logging.h>

#include "cpp_command.h"
#include "char_buffer.h"
#include "close_buffer_command.h"
#include "command.h"
#include "command_mode.h"
#include "dirname.h"
#include "goto_command.h"
#include "file_link_mode.h"
#include "find_mode.h"
#include "help_command.h"
#include "insert_mode.h"
#include "lazy_string_append.h"
#include "line_prompt_mode.h"
#include "list_buffers_command.h"
#include "map_mode.h"
#include "navigate_command.h"
#include "noop_command.h"
#include "open_directory_command.h"
#include "open_file_command.h"
#include "quit_command.h"
#include "record_command.h"
#include "repeat_mode.h"
#include "run_command_handler.h"
#include "run_cpp_command.h"
#include "search_command.h"
#include "search_handler.h"
#include "send_end_of_file_command.h"
#include "set_variable_command.h"
#include "substring.h"
#include "terminal.h"
#include "transformation.h"
#include "transformation_delete.h"
#include "transformation_move.h"
#include "src/wstring.h"

namespace {
using std::advance;
using std::ceil;
using std::make_pair;
using namespace afc::editor;

class Delete : public Command {
 public:
  Delete(DeleteOptions delete_options) : delete_options_(delete_options) {}

  const wstring Description() {
    if (delete_options_.delete_region) {
      return L"deletes the current item (char, word, line...)";
    }
    return L"copies current item (char, word, ...) to the paste buffer.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;

    switch (editor_state->structure()) {
      case CHAR:
      case WORD:
      case LINE:
      case BUFFER:
      case CURSOR:
      case TREE:
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          DeleteOptions options = delete_options_;
          options.modifiers = editor_state->modifiers();
          editor_state->ApplyToCurrentBuffer(NewDeleteTransformation(options));
          editor_state->ScheduleRedraw();
        }
        break;

      case MARK:
        // TODO: Implement.
        editor_state->SetStatus(L"Oops, delete mark is not implemented.");
        break;

      case PAGE:
        // TODO: Implement.
        editor_state->SetStatus(L"Oops, delete page is not yet implemented.");
        break;

      case SEARCH:
        // TODO: Implement.
        editor_state->SetStatus(
            L"Ooops, delete search is not yet implemented.");
        break;
    }

    LOG(INFO) << "After applying delete transformation: "
              << editor_state->modifiers();
    editor_state->ResetModifiers();
  }

 private:
  const DeleteOptions delete_options_;
};

// TODO: Replace with insert.  Insert should be called 'type'.
class Paste : public Command {
 public:
  const wstring Description() {
    return L"pastes the last deleted text";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto it = editor_state->buffers()->find(OpenBuffer::kPasteBuffer);
    if (it == editor_state->buffers()->end()) {
      editor_state->SetStatus(L"No text to paste.");
      return;
    }
    if (it == editor_state->current_buffer()) {
      editor_state->SetStatus(L"You shall not paste into the paste buffer.");
      return;
    }
    auto buffer = editor_state->current_buffer()->second;
    buffer->CheckPosition();
    buffer->MaybeAdjustPositionCol();
    editor_state->ApplyToCurrentBuffer(NewInsertBufferTransformation(
        it->second, editor_state->repetitions(), END));
    editor_state->ResetRepetitions();
    editor_state->ResetInsertionModifier();
    editor_state->ScheduleRedraw();
  }
};

class UndoCommand : public Command {
 public:
  const wstring Description() {
    return L"undoes the last change to the current buffer";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    editor_state->current_buffer()->second->Undo(editor_state);
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
    editor_state->ScheduleRedraw();
  }
};

class GotoPreviousPositionCommand : public Command {
 public:
  const wstring Description() {
    return L"go back to previous position";
  }

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
          editor_state->current_buffer()->second->position();
      if (it != editor_state->buffers()->end()
          && (pos.buffer_name != editor_state->current_buffer()->first
              || (editor_state->structure() <= LINE
                  && pos.position.line != current_position.line)
              || (editor_state->structure() <= CHAR
                  && pos.position.column != current_position.column))) {
        LOG(INFO) << "Jumping to position: " << it->second->name() << " "
                  << pos.position;
        editor_state->set_current_buffer(it);
        it->second->set_position(pos.position);
        editor_state->ScheduleRedraw();
        editor_state->set_repetitions(editor_state->repetitions() - 1);
      }
    }
  }
};

class LineUp : public Command {
 public:
  const wstring Description();
  static void Move(int c, EditorState* editor_state, Structure structure);
  void ProcessInput(wint_t c, EditorState* editor_state);
};

class LineDown : public Command {
 public:
  const wstring Description();
  static void Move(int c, EditorState* editor_state, Structure structure);
  void ProcessInput(wint_t c, EditorState* editor_state);
};

class PageUp : public Command {
 public:
  const wstring Description();
  static void Move(int c, EditorState* editor_state);
  void ProcessInput(wint_t c, EditorState* editor_state);
};

class PageDown : public Command {
 public:
  const wstring Description();
  void ProcessInput(wint_t c, EditorState* editor_state);
};

class MoveForwards : public Command {
 public:
  const wstring Description();
  void ProcessInput(wint_t c, EditorState* editor_state);
  static void Move(int c, EditorState* editor_state);
};

class MoveBackwards : public Command {
 public:
  const wstring Description();
  void ProcessInput(wint_t c, EditorState* editor_state);
  static void Move(int c, EditorState* editor_state);
};

const wstring LineUp::Description() {
  return L"moves up one line";
}

/* static */ void LineUp::Move(
    int c, EditorState* editor_state, Structure structure) {
  if (editor_state->direction() == BACKWARDS || structure == TREE) {
    editor_state->set_direction(ReverseDirection(editor_state->direction()));
    LineDown::Move(c, editor_state, structure);
    return;
  }
  if (!editor_state->has_current_buffer()) { return; }
  switch (structure) {
    case CHAR:
      editor_state->set_structure(LINE);
      MoveBackwards::Move(c, editor_state);
      break;

    case WORD:
      // Move in whole pages.
      editor_state->set_repetitions(
          editor_state->repetitions() * editor_state->visible_lines());
      Move(c, editor_state, CHAR);
      break;

    case TREE:
      CHECK(false);  // Handled above.
      break;

    default:
      editor_state->MoveBufferBackwards(editor_state->repetitions());
      editor_state->ScheduleRedraw();
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
  editor_state->ResetDirection();
}

void LineUp::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state, editor_state->structure());
}

const wstring LineDown::Description() {
  return L"moves down one line";
}

/* static */ void LineDown::Move(
    int c, EditorState* editor_state, Structure structure) {
  if (editor_state->direction() == BACKWARDS && structure != TREE) {
    editor_state->set_direction(FORWARDS);
    LineUp::Move(c, editor_state, structure);
    return;
  }
  if (!editor_state->has_current_buffer()) { return; }
  switch (structure) {
    case CHAR:
      editor_state->set_structure(LINE);
      MoveForwards::Move(c, editor_state);
      break;

    case WORD:
      // Move in whole pages.
      editor_state->set_repetitions(
          editor_state->repetitions() * editor_state->visible_lines());
      Move(c, editor_state, CHAR);
      break;

    case TREE:
      {
        if (!editor_state->has_current_buffer()) { return; }
        auto buffer = editor_state->current_buffer()->second;
        if (editor_state->direction() == BACKWARDS) {
          if (buffer->tree_depth() > 0) {
            buffer->set_tree_depth(buffer->tree_depth() - 1);
          }
        } else if (editor_state->direction() == FORWARDS) {
          auto root = buffer->parse_tree();
          const ParseTree* tree = buffer->current_tree(root.get());
          if (!tree->children.empty()) {
            buffer->set_tree_depth(buffer->tree_depth() + 1);
          }
        } else {
          CHECK(false) << "Invalid direction: " << editor_state->direction();
        }
      }
      editor_state->ResetMode();
      editor_state->ResetDirection();
      editor_state->ResetStructure();
      editor_state->ScheduleRedraw();
      break;

    default:
      editor_state->MoveBufferForwards(editor_state->repetitions());
      editor_state->ScheduleRedraw();
  }
  editor_state->ResetStructure();
  editor_state->ResetRepetitions();
  editor_state->ResetDirection();
}

void LineDown::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state, editor_state->structure());
}

const wstring PageUp::Description() {
  return L"moves up one page";
}

void PageUp::ProcessInput(wint_t c, EditorState* editor_state) {
  editor_state->set_repetitions(
      editor_state->repetitions() * editor_state->visible_lines());
  editor_state->ResetStructure();
  LineUp::Move(c, editor_state, editor_state->structure());
}

const wstring PageDown::Description() {
  return L"moves down one page";
}

void PageDown::ProcessInput(wint_t c, EditorState* editor_state) {
  editor_state->set_repetitions(
      editor_state->repetitions() * editor_state->visible_lines());
  editor_state->ResetStructure();
  LineDown::Move(c, editor_state, editor_state->structure());
}

const wstring MoveForwards::Description() {
  return L"moves forwards";
}

void MoveForwards::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state);
}

/* static */ void MoveForwards::Move(int c, EditorState* editor_state) {
  switch (editor_state->structure()) {
    case CHAR:
    case WORD:
    case LINE:
    case MARK:
    case CURSOR:
    case TREE:
      {
        if (!editor_state->has_current_buffer()) { return; }
        editor_state->ApplyToCurrentBuffer(
            NewMoveTransformation(editor_state->modifiers()));
        editor_state->ResetRepetitions();
        editor_state->ResetStructure();
        editor_state->ResetDirection();
      }
      break;

    case SEARCH:
      {
        SearchOptions options;
        options.search_query = editor_state->last_search_query();
        options.starting_position =
            editor_state->current_buffer()->second->position();
        JumpToNextMatch(editor_state, options);
      }
      editor_state->ResetMode();
      editor_state->ResetDirection();
      editor_state->ResetStructure();
      editor_state->ScheduleRedraw();
      break;

    default:
      LineDown::Move(c, editor_state,
          LowerStructure(
              LowerStructure(editor_state->structure())));
  }
}

const wstring MoveBackwards::Description() {
  return L"moves backwards";
}

void MoveBackwards::ProcessInput(wint_t c, EditorState* editor_state) {
  Move(c, editor_state);
}

/* static */ void MoveBackwards::Move(int c, EditorState* editor_state) {
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
    MoveForwards::Move(c, editor_state);
    return;
  }
  switch (editor_state->structure()) {
    case CHAR:
    case WORD:
    case LINE:
    case MARK:
    case CURSOR:
    case TREE:
      {
        if (!editor_state->has_current_buffer()) { return; }
        editor_state->set_direction(
            ReverseDirection(editor_state->direction()));
        MoveForwards::Move(c, editor_state);
        return;
      }
      break;

    case SEARCH:
      editor_state->set_direction(BACKWARDS);
      {
        SearchOptions options;
        options.search_query = editor_state->last_search_query();
        options.starting_position =
            editor_state->current_buffer()->second->position();
        JumpToNextMatch(editor_state, options);
      }
      editor_state->ResetMode();
      editor_state->ResetDirection();
      editor_state->ResetStructure();
      editor_state->ScheduleRedraw();
      break;

    default:
      LineUp::Move(c, editor_state,
          LowerStructure(
              LowerStructure(editor_state->structure())));
  }
}

class EnterInsertModeCommand : public Command {
 public:
  const wstring Description() {
    return L"enters insert mode";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    EnterInsertMode(editor_state);
  }
};

class EnterFindMode : public Command {
 public:
  const wstring Description() {
    return L"finds occurrences of a character";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->set_mode(NewFindMode());
  }
};

class InsertionModifierCommand : public Command {
 public:
  const wstring Description() {
    return L"activates replace modifier (overwrites text on insertions)";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->insertion_modifier() == Modifiers::INSERT) {
      editor_state->set_insertion_modifier(Modifiers::REPLACE);
    } else if (editor_state->default_insertion_modifier() == Modifiers::INSERT) {
      editor_state->set_default_insertion_modifier(Modifiers::REPLACE);
    } else {
      editor_state->set_default_insertion_modifier(Modifiers::INSERT);
      editor_state->ResetInsertionModifier();
    }
  }
};

class ReverseDirectionCommand : public Command {
 public:
  const wstring Description() {
    return L"reverses the direction of the next command";
  }

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

void SetRepetitions(EditorState* editor_state, int number) {
  editor_state->set_repetitions(number);
}

class SetStructureCommand : public Command {
 public:
  SetStructureCommand(Structure value, const wstring& description)
      : value_(value), description_(description) {}

  const wstring Description() {
    return L"sets the structure: " + description_;
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->structure() != value_) {
      editor_state->set_structure(value_);
      editor_state->set_sticky_structure(false);
    } else if (!editor_state->sticky_structure()) {
      editor_state->set_sticky_structure(true);
    } else {
      editor_state->set_structure(CHAR);
      editor_state->set_sticky_structure(false);
    }
  }

 private:
  Structure value_;
  const wstring description_;
};

class SetStrengthCommand : public Command {
 public:
  SetStrengthCommand(Modifiers::Strength value,
                     Modifiers::Strength extreme_value,
                     const wstring& description)
      : value_(value), extreme_value_(extreme_value),
        description_(description) {}

  const wstring Description() {
    return L"sets the strength: " + description_;
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    Modifiers modifiers(editor_state->modifiers());
    if (modifiers.strength == value_) {
      modifiers.strength = extreme_value_;
    } else if (modifiers.strength == extreme_value_) {
      modifiers.strength = Modifiers::DEFAULT;
    } else {
      modifiers.strength = value_;
    }
    editor_state->set_modifiers(modifiers);
  }

 private:
  Modifiers::Strength value_;
  Modifiers::Strength extreme_value_;
  const wstring description_;
};

class SetStructureModifierCommand : public Command {
 public:
  SetStructureModifierCommand(
      Modifiers::StructureRange value, const wstring& description)
      : value_(value), description_(description) {}

  const wstring Description() {
    return L"sets the structure modifier: " + description_;
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->set_structure_range(
        editor_state->structure_range() == value_
            ? Modifiers::ENTIRE_STRUCTURE
            : value_);
  }

 private:
  Modifiers::StructureRange value_;
  const wstring description_;
};

class NumberMode : public Command {
 public:
  NumberMode(function<void(EditorState*, int)> consumer)
      : description_(L""), consumer_(consumer) {}

  NumberMode(
      const wstring& description, function<void(EditorState*, int)> consumer)
      : description_(description), consumer_(consumer) {}

  const wstring Description() {
    return description_;
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    editor_state->set_mode(NewRepeatMode(consumer_));
    if (c < '0' || c > '9') { return; }
    editor_state->mode()->ProcessInput(c, editor_state);
  }

 private:
  const wstring description_;
  function<void(EditorState*, int)> consumer_;
};

class ActivateLink : public Command {
 public:
  const wstring Description() {
    return L"activates the current link (if any)";
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    if (buffer->current_line() == nullptr) { return; }

    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr && target != buffer) {
      editor_state->ResetStatus();
      auto it = editor_state->buffers()->find(target->name());
      if (it == editor_state->buffers()->end()) { return; }
      editor_state->set_current_buffer(it);
      editor_state->PushCurrentPosition();
      editor_state->ScheduleRedraw();
      editor_state->ResetMode();
      return;
    }

    buffer->MaybeAdjustPositionCol();
    wstring line = buffer->current_line()->ToString();

    const wstring& path_characters =
        buffer->read_string_variable(buffer->variable_path_characters());

    size_t start = line.find_last_not_of(
        path_characters, buffer->current_position_col());
    if (start != line.npos) {
      line = line.substr(start + 1);
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
        buffer->read_string_variable(buffer->variable_path()));
    // And a fall-back for the current buffer being a file:
    options.initial_search_paths.push_back(
        Dirname(options.initial_search_paths[0]));
    LOG(INFO) << "Initial search path: " << options.initial_search_paths[0];

    OpenFile(options);
  }
};

class ResetStateCommand : public Command {
 public:
  const wstring Description() {
    return L"Resets the state of the editor.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->ResetStatus();
    editor_state->set_modifiers(Modifiers());
  }
};

class HardRedrawCommand : public Command {
 public:
  const wstring Description() {
    return L"Redraws the screen";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->set_screen_needs_hard_redraw(true);
  }
};

void RunCppFileHandler(const wstring& input, EditorState* editor_state) {
  editor_state->ResetMode();
  if (!editor_state->has_current_buffer()) { return; }
  wstring adjusted_input;
  if (!ResolvePath(editor_state, input, &adjusted_input, nullptr, nullptr)) {
    editor_state->SetStatus(L"File not found: " + input);
    return;
  }

  auto buffer = editor_state->current_buffer()->second;
  if (editor_state->structure() == LINE) {
    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr) {
      buffer = target;
    }
    editor_state->ResetModifiers();
  }
  for (size_t i = 0; i < editor_state->repetitions(); i++) {
    buffer->EvaluateFile(editor_state, adjusted_input);
  }
  editor_state->ResetRepetitions();
}

class RunCppFileCommand : public Command {
  const wstring Description() {
    return L"runs a command from a file";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    PromptOptions options;
    options.prompt = L"cmd ";
    options.history_file = L"editor_commands";
    options.initial_value = buffer->read_string_variable(
        OpenBuffer::variable_editor_commands_path()),
    options.handler = RunCppFileHandler;
    options.predictor = FilePredictor;
    Prompt(editor_state, std::move(options));
  }
};

class SwitchCaseTransformation : public Transformation {
 public:
  void Apply(EditorState* editor_state, OpenBuffer* buffer, Result* result)
      const override {
    buffer->AdjustLineColumn(&result->cursor);
    LineColumn start, end;
    if (!buffer->FindPartialRange(
            editor_state->modifiers(), result->cursor, &start, &end)) {
      editor_state->SetStatus(L"Structure not handled.");
      return;
    }
    CHECK_LE(start, end);
    unique_ptr<TransformationStack> stack(new TransformationStack());
    stack->PushBack(NewGotoPositionTransformation(start));
    shared_ptr<OpenBuffer> buffer_to_insert(
        new OpenBuffer(editor_state, L"- text inserted"));
    VLOG(5) << "Switch Case Transformation at " << result->cursor << ": "
            << editor_state->modifiers() << ": Range [" << start << ", "
            << end << ")";
    LineColumn i = start;
    while (i < end) {
      auto line = buffer->LineAt(i.line);
      if (line == nullptr) {
        break;
      }
      if (i.column >= line->size()) {
        // Switch to the next line.
        i = LineColumn(i.line + 1);
        DeleteOptions options;
        options.copy_to_paste_buffer = false;
        stack->PushBack(NewDeleteCharactersTransformation(options));
        buffer_to_insert->AppendEmptyLine(editor_state);
        continue;
      }
      wchar_t c = line->get(i.column);
      buffer_to_insert->AppendToLastLine(editor_state,
          NewCopyString(wstring(1, iswupper(c) ? towlower(c) : towupper(c))));
      DeleteOptions options;
      options.copy_to_paste_buffer = false;
      stack->PushBack(NewDeleteCharactersTransformation(options));

      // Increment i.
      i.column++;
    }
    stack->PushBack(NewInsertBufferTransformation(buffer_to_insert, 1, END));
    stack->PushBack(NewGotoPositionTransformation(
        editor_state->direction() == FORWARDS ? end : start));
    stack->Apply(editor_state, buffer, result);
    editor_state->ResetModifiers();
    editor_state->ScheduleRedraw();
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(new SwitchCaseTransformation());
  }
};

class SwitchCaseCommand : public Command {
 public:
  const wstring Description() {
    return L"Switches the case of the current character.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    editor_state->ApplyToCurrentBuffer(
        unique_ptr<Transformation>(new SwitchCaseTransformation()));
  }
};

}  // namespace

namespace afc {
namespace editor {

namespace {
template <typename T>
void ToggleBoolVariable(
    EditorState* editor_state, wstring binding, wstring variable_name,
    T* output) {
  wstring command =
      L"// Toggle buffer variable: " + variable_name + L"\n"
      + L"Buffer tmp_buffer = CurrentBuffer();"
      + L"tmp_buffer.set_" + variable_name + L"("
      + L"!tmp_buffer." + variable_name + L"()" + L"); "
      + L"SetStatus(\"" + variable_name + L" := \" + (tmp_buffer."
      + variable_name + L"() ? \"ON\" : \"OFF\"));";
  LOG(INFO) << "Command: " << command;
  MapMode::RegisterEntry(binding,
      NewCppCommand(editor_state->environment(), command).release(),
      output);
}

template <typename T>
void ToggleIntVariable(
    EditorState* editor_state, wstring binding, wstring variable_name,
    int default_value, T* output) {
  wstring command =
      L"// Toggle buffer variable: " + variable_name + L"\n"
      + L"Buffer tmp_buffer = CurrentBuffer();"
      + L"tmp_buffer.set_" + variable_name + L"("
      + L"tmp_buffer." + variable_name + L"() != 0 ? 0 : "
      + std::to_wstring(default_value) + L"); "
      + L"SetStatus(\"" + variable_name + L" := \" + tostring(tmp_buffer."
      + variable_name + L"()));";
  LOG(INFO) << "Command: " << command;
  MapMode::RegisterEntry(binding,
      NewCppCommand(editor_state->environment(), command).release(),
      output);
}
}  // namespace

using std::map;
using std::unique_ptr;

std::function<unique_ptr<EditorMode>(void)> NewCommandModeSupplier(
    EditorState* editor_state) {
  auto commands_map = std::make_shared<map<vector<wint_t>, Command*>>();
  auto Register = MapMode::RegisterEntry;
  Register(L"aq", NewQuitCommand().release(), commands_map.get());
  Register(L"ad", NewCloseBufferCommand().release(), commands_map.get());
  Register(L"aw",
      NewCppCommand(editor_state->environment(),
          L"// Save the current buffer.\n"
          L"editor.SaveCurrentBuffer();").release(),
      commands_map.get());
  Register(L"av", NewSetVariableCommand().release(), commands_map.get());
  Register(L"ac", new RunCppFileCommand(), commands_map.get());
  Register(L"aC", NewRunCppCommand().release(), commands_map.get());
  Register(L"a.", NewOpenDirectoryCommand().release(), commands_map.get());
  Register(L"al", NewListBuffersCommand().release(), commands_map.get());
  Register(L"ar",
      NewCppCommand(editor_state->environment(),
          L"// Reload the current buffer.\n"
          L"editor.ReloadCurrentBuffer();").release(),
      commands_map.get());
  Register(L"ae", NewSendEndOfFileCommand().release(), commands_map.get());
  Register(L"ao", NewOpenFileCommand().release(), commands_map.get());
  {
    PromptOptions options;
    options.prompt = L"...$ ";
    options.history_file = L"commands";
    options.handler = RunMultipleCommandsHandler;
    Register(L"aF",
        NewLinePromptCommand(
            L"forks a command for each line in the current buffer",
            [options](EditorState*) { return options; }).release(),
        commands_map.get());
  }
  Register(L"af", NewForkCommand().release(), commands_map.get());

  Register(L"+",
      NewCppCommand(editor_state->environment(),
          L"// Create a new cursor at the current position.\n"
          L"editor.CreateCursor();").release(),
      commands_map.get());
  Register(L"-",
      NewCppCommand(editor_state->environment(),
          L"// Destroy current cursor(s) and jump to next.\n"
          L"editor.DestroyCursor();").release(),
      commands_map.get());
  Register(L"=",
      NewCppCommand(editor_state->environment(),
          L"// Destroy cursors other than the current one.\n"
          L"editor.DestroyOtherCursors();").release(),
      commands_map.get());
  Register(L"_",
      NewCppCommand(editor_state->environment(),
          L"// Toggles whether operations apply to all cursors.\n"
          L"CurrentBuffer().set_multiple_cursors(\n"
          L"    !CurrentBuffer().multiple_cursors());").release(),
      commands_map.get());
  Register(L"Ct",
      NewCppCommand(editor_state->environment(),
          L"// Toggles the active cursors with the previous set.\n"
          L"editor.ToggleActiveCursors();").release(),
      commands_map.get());
  Register(L"C+",
      NewCppCommand(editor_state->environment(),
          L"// Pushes the active cursors to the stack.\n"
          L"editor.PushActiveCursors();").release(),
      commands_map.get());
  Register(L"C-",
      NewCppCommand(editor_state->environment(),
          L"// Pops active cursors from the stack.\n"
          L"editor.PopActiveCursors();").release(),
      commands_map.get());
  Register(L"C!",
      NewCppCommand(editor_state->environment(),
          L"// Set active cursors to the marks on this buffer.\n"
          L"editor.SetActiveCursorsToMarks();").release(),
      commands_map.get());

  Register(L"i", new EnterInsertModeCommand(), commands_map.get());
  Register(L"f", new EnterFindMode(), commands_map.get());
  Register(L"r", new ReverseDirectionCommand(), commands_map.get());
  Register(L"R", new InsertionModifierCommand(), commands_map.get());

  Register(L"/", NewSearchCommand().release(), commands_map.get());
  Register(L"g", NewGotoCommand().release(), commands_map.get());

  Register(L"w", new SetStructureCommand(WORD, L"word"), commands_map.get());
  Register(L"e", new SetStructureCommand(LINE, L"line"), commands_map.get());
  Register(L"E", new SetStructureCommand(PAGE, L"page"), commands_map.get());
  Register(L"F", new SetStructureCommand(SEARCH, L"search"), commands_map.get());
  Register(L"c", new SetStructureCommand(CURSOR, L"cursor"), commands_map.get());
  Register(L"B", new SetStructureCommand(BUFFER, L"buffer"), commands_map.get());
  Register(L"!", new SetStructureCommand(MARK, L"mark"), commands_map.get());
  Register(L"t", new SetStructureCommand(TREE, L"tree"), commands_map.get());

  Register(L"W", new SetStrengthCommand(
      Modifiers::WEAK, Modifiers::VERY_WEAK, L"weak"), commands_map.get());
  Register(L"S", new SetStrengthCommand(
      Modifiers::STRONG, Modifiers::VERY_STRONG, L"strong"), commands_map.get());

  Register(L"d", new Delete(DeleteOptions()), commands_map.get());
  Register(L"p", new Paste(), commands_map.get());

  DeleteOptions copy_options;
  copy_options.delete_region = false;
  Register(L"sc", new Delete(copy_options), commands_map.get());
  Register(L"u", new UndoCommand(), commands_map.get());
  Register(L"\n", new ActivateLink(), commands_map.get());

  Register(L"b", new GotoPreviousPositionCommand(), commands_map.get());
  Register(L"n", NewNavigateCommand().release(), commands_map.get());
  Register(L"j", new LineDown(), commands_map.get());
  Register(L"k", new LineUp(), commands_map.get());
  Register(L"l", new MoveForwards(), commands_map.get());
  Register(L"h", new MoveBackwards(), commands_map.get());

  Register(L"~", new SwitchCaseCommand(), commands_map.get());

  Register(L"sr", NewRecordCommand().release(), commands_map.get());
  Register(L"\t", NewFindCompletionCommand().release(), commands_map.get());

  Register(L".",
      NewCppCommand(editor_state->environment(),
          L"// Repeats the last command.\n"
          L"editor.RepeatLastTransformation();").release(),
      commands_map.get());

  ToggleBoolVariable(editor_state, L"vp", L"paste_mode", commands_map.get());
  ToggleBoolVariable(editor_state, L"vs", L"show_in_buffers_list",
      commands_map.get());
  ToggleBoolVariable(editor_state, L"vf", L"follow_end_of_file",
      commands_map.get());
  ToggleBoolVariable(editor_state, L"v/c", L"search_case_sensitive",
      commands_map.get());
  ToggleIntVariable(editor_state, L"vc", L"buffer_list_context_lines", 10,
      commands_map.get());

  Register(L"?",
      NewHelpCommand(*commands_map,
                     L"command mode").release(), commands_map.get());

  Register({Terminal::ESCAPE}, new ResetStateCommand(), commands_map.get());

  Register(L"[", new SetStructureModifierCommand(
      Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION,
      L"from the beggining to the current position"), commands_map.get());
  Register(L"]", new SetStructureModifierCommand(
      Modifiers::FROM_CURRENT_POSITION_TO_END,
      L"from the current position to the end"), commands_map.get());
  Register({Terminal::CTRL_L}, new HardRedrawCommand(), commands_map.get());
  Register(L"0", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"1", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"2", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"3", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"4", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"5", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"6", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"7", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"8", new NumberMode(SetRepetitions), commands_map.get());
  Register(L"9", new NumberMode(SetRepetitions), commands_map.get());
  Register({Terminal::DOWN_ARROW}, new LineDown(), commands_map.get());
  Register({Terminal::UP_ARROW}, new LineUp(), commands_map.get());
  Register({Terminal::LEFT_ARROW}, new MoveBackwards(), commands_map.get());
  Register({Terminal::RIGHT_ARROW}, new MoveForwards(), commands_map.get());
  Register({Terminal::PAGE_DOWN}, new PageDown(), commands_map.get());
  Register({Terminal::PAGE_UP}, new PageUp(), commands_map.get());

  return [commands_map]() {
    return unique_ptr<MapMode>(new MapMode(commands_map, NoopCommand()));
  };
}

}  // namespace afc
}  // namespace editor
