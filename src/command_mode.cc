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
#include "save_buffer_command.h"
#include "search_command.h"
#include "search_handler.h"
#include "send_end_of_file_command.h"
#include "set_variable_command.h"
#include "substring.h"
#include "terminal.h"
#include "transformation.h"
#include "transformation_delete.h"
#include "transformation_move.h"
#include "wstring.h"

namespace {
using std::advance;
using std::ceil;
using std::make_pair;
using namespace afc::editor;

class Delete : public Command {
 public:
  const wstring Description() {
    return L"deletes the current item (char, word, line ...)";
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
        if (editor_state->has_current_buffer()) {
          auto buffer = editor_state->current_buffer()->second;
          editor_state->ApplyToCurrentBuffer(
              NewDeleteTransformation(editor_state->modifiers(), true));
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
      return;
    }
    while (editor_state->repetitions() > 0) {
      if (!editor_state->MovePositionsStack(editor_state->direction())) {
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
        editor_state->set_current_buffer(it);
        it->second->set_position(pos.position);
        it->second->Enter(editor_state);
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
  if (editor_state->direction() == BACKWARDS) {
    editor_state->set_direction(FORWARDS);
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
  if (editor_state->direction() == BACKWARDS) {
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
        SearchHandler(editor_state, options);
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
        SearchHandler(editor_state, options);
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

class EnterInsertMode : public Command {
 public:
  const wstring Description() {
    return L"enters insert mode";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    afc::editor::EnterInsertMode(editor_state);
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
    if (buffer->current_line()->activate() != nullptr) {
      buffer->current_line()->activate()->ProcessInput(c, editor_state);
    } else {
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

      OpenFileOptions options;
      options.editor_state = editor_state;
      options.path = line;
      options.ignore_if_not_found = true;
      OpenFile(options);
    }
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
  auto buffer = editor_state->current_buffer()->second;
  for (size_t i = 0; i < editor_state->repetitions(); i++) {
    buffer->EvaluateFile(editor_state, input);
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
    options.prompt = L"cmd";
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
  void Apply(
      EditorState* editor_state, OpenBuffer* buffer, Result* result) const {
    LineColumn start;
    LineColumn end;
    if (!buffer->FindPartialRange(
            editor_state->modifiers(), buffer->position(), &start, &end)) {
      editor_state->SetStatus(L"Structure not handled.");
      return;
    }
    CHECK_LE(start, end);
    unique_ptr<TransformationStack> stack(new TransformationStack);
    stack->PushBack(NewGotoPositionTransformation(start));
    shared_ptr<OpenBuffer> buffer_to_insert(
        new OpenBuffer(editor_state, L"- text inserted"));
    VLOG(5) << "Switch Case Transformation: [" << start << ", " << end << ")";
    LineColumn i = start;
    while (i < end) {
      auto line = buffer->LineAt(i.line);
      if (i.column == line->size()) {
        // Switch to the next line.
        i = LineColumn(i.line + 1);
        stack->PushBack(NewDeleteCharactersTransformation(Modifiers(), false));
        buffer_to_insert->AppendEmptyLine(editor_state);
        continue;
      }
      wchar_t c = line->get(i.column);
      buffer_to_insert->AppendToLastLine(editor_state,
          NewCopyString(wstring(1, iswupper(c) ? towlower(c) : towupper(c))));
      stack->PushBack(NewDeleteCharactersTransformation(Modifiers(), false));

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

class RepeatLastTransformationCommand : public Command {
 public:
  const wstring Description() {
    return L"Repeats the last command.";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    editor_state
        ->current_buffer()->second->RepeatLastTransformation(editor_state);
    editor_state->ScheduleRedraw();
  }
};

// TODO: This leaks a lot of memory... fix that.
static const map<vector<wint_t>, Command*> GetCommandModeMap(
    EditorState* editor_state) {
  map<vector<wint_t>, Command*> output;
  auto Register = MapMode::RegisterEntry;
  Register(L"aq", NewQuitCommand().release(), &output);
  Register(L"ad", NewCloseBufferCommand().release(), &output);
  Register(L"aw", NewSaveBufferCommand().release(), &output);
  Register(L"av", NewSetVariableCommand().release(), &output);
  Register(L"ac", new RunCppFileCommand(), &output);
  Register(L"aC", NewRunCppCommand().release(), &output);
  Register(L"a.", NewOpenDirectoryCommand().release(), &output);
  Register(L"al", NewListBuffersCommand().release(), &output);
  Register(L"ar",
      NewCppCommand(editor_state->environment(),
          L"// Reload the current buffer.\n"
          L"editor.ReloadCurrentBuffer();").release(),
      &output);
  Register(L"ae", NewSendEndOfFileCommand().release(), &output);
  Register(L"ao", NewOpenFileCommand().release(), &output);
  {
    PromptOptions options;
    options.prompt = L"...$";
    options.history_file = L"commands";
    options.handler = RunMultipleCommandsHandler;
    Register(L"aF",
        NewLinePromptCommand(
            L"forks a command for each line in the current buffer",
            std::move(options)).release(),
        &output);
  }
  Register(L"af", NewForkCommand().release(), &output);

  Register(L"+",
      NewCppCommand(editor_state->environment(),
          L"// Create a new cursor at the current position.\n"
          L"editor.CreateCursor();").release(),
      &output);
  Register(L"-",
      NewCppCommand(editor_state->environment(),
          L"// Destroy current cursor(s) and jump to next.\n"
          L"editor.DestroyCursor();").release(),
      &output);
  Register(L"=",
      NewCppCommand(editor_state->environment(),
          L"// Destroy cursors other than the current one.\n"
          L"editor.DestroyOtherCursors();").release(),
      &output);
  Register(L"_",
      NewCppCommand(editor_state->environment(),
          L"// Toggles whether operations apply to all cursors.\n"
          L"CurrentBuffer().set_multiple_cursors(\n"
          L"    !CurrentBuffer().multiple_cursors());").release(),
      &output);

  Register(L"i", new EnterInsertMode(), &output);
  Register(L"f", new EnterFindMode(), &output);
  Register(L"r", new ReverseDirectionCommand(), &output);
  Register(L"R", new InsertionModifierCommand(), &output);

  Register(L"/", NewSearchCommand().release(), &output);
  Register(L"g", NewGotoCommand().release(), &output);

  Register(L"w", new SetStructureCommand(WORD, L"word"), &output);
  Register(L"e", new SetStructureCommand(LINE, L"line"), &output);
  Register(L"E", new SetStructureCommand(PAGE, L"page"), &output);
  Register(L"F", new SetStructureCommand(SEARCH, L"search"), &output);
  Register(L"c", new SetStructureCommand(CURSOR, L"cursor"), &output);
  Register(L"B", new SetStructureCommand(BUFFER, L"buffer"), &output);
  Register(L"!", new SetStructureCommand(MARK, L"mark"), &output);

  Register(L"W", new SetStrengthCommand(
      Modifiers::WEAK, Modifiers::VERY_WEAK, L"weak"), &output);
  Register(L"S", new SetStrengthCommand(
      Modifiers::STRONG, Modifiers::VERY_STRONG, L"strong"), &output);

  Register(L"d", new Delete(), &output);
  Register(L"p", new Paste(), &output);
  Register(L"u", new UndoCommand(), &output);
  Register(L"\n", new ActivateLink(), &output);

  Register(L"b", new GotoPreviousPositionCommand(), &output);
  Register(L"n", NewNavigateCommand().release(), &output);
  Register(L"j", new LineDown(), &output);
  Register(L"k", new LineUp(), &output);
  Register(L"l", new MoveForwards(), &output);
  Register(L"h", new MoveBackwards(), &output);

  Register(L"~", new SwitchCaseCommand(), &output);

  Register(L"sr", NewRecordCommand().release(), &output);

  Register(L".", new RepeatLastTransformationCommand(), &output);
  Register(L"?",
      NewHelpCommand(output, L"command mode").release(), &output);

  Register({Terminal::ESCAPE}, new ResetStateCommand(), &output);

  Register(L"[", new SetStructureModifierCommand(
      Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION,
      L"from the beggining to the current position"), &output);
  Register(L"]", new SetStructureModifierCommand(
      Modifiers::FROM_CURRENT_POSITION_TO_END,
      L"from the current position to the end"), &output);
  Register({Terminal::CTRL_L}, new HardRedrawCommand(), &output);
  Register(L"0", new NumberMode(SetRepetitions), &output);
  Register(L"1", new NumberMode(SetRepetitions), &output);
  Register(L"2", new NumberMode(SetRepetitions), &output);
  Register(L"3", new NumberMode(SetRepetitions), &output);
  Register(L"4", new NumberMode(SetRepetitions), &output);
  Register(L"5", new NumberMode(SetRepetitions), &output);
  Register(L"6", new NumberMode(SetRepetitions), &output);
  Register(L"7", new NumberMode(SetRepetitions), &output);
  Register(L"8", new NumberMode(SetRepetitions), &output);
  Register(L"9", new NumberMode(SetRepetitions), &output);
  Register({Terminal::DOWN_ARROW}, new LineDown(), &output);
  Register({Terminal::UP_ARROW}, new LineUp(), &output);
  Register({Terminal::LEFT_ARROW}, new MoveBackwards(), &output);
  Register({Terminal::RIGHT_ARROW}, new MoveForwards(), &output);
  Register({Terminal::PAGE_DOWN}, new PageDown(), &output);
  Register({Terminal::PAGE_UP}, new PageUp(), &output);

  return output;
}

}  // namespace

namespace afc {
namespace editor {

using std::map;
using std::unique_ptr;

unique_ptr<EditorMode> NewCommandMode(EditorState* editor_state) {
  return unique_ptr<MapMode>(
      new MapMode(GetCommandModeMap(editor_state), NoopCommand()));
}

}  // namespace afc
}  // namespace editor
