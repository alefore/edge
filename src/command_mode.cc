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
#include "delete_mode.h"
#include "dirname.h"
#include "goto_command.h"
#include "file_link_mode.h"
#include "find_mode.h"
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
    if (delete_options_.modifiers.delete_type == Modifiers::DELETE_CONTENTS) {
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
      editor_state->SetWarningStatus(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    if (it == editor_state->current_buffer()) {
      const static wstring errors[] = {
          L"You shall not paste into the paste buffer.",
          L"Nope.",
          L"Bad things would happen if you pasted into the buffer.",
          L"There could be endless loops if you pasted into this buffer.",
          L"This is not supported.",
          L"Go to a different buffer first?",
          L"The paste buffer is not for pasting into.",
          L"This editor is too important for me to allow you to jeopardize it.",
          L"",
      };
      static int current_message = 0;
      editor_state->SetWarningStatus(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    auto buffer = editor_state->current_buffer()->second;
    if (buffer->fd() != -1) {
      string text = ToByteString(it->second->ToString());
      for (size_t i = 0; i < editor_state->repetitions(); i++) {
        if (write(buffer->fd(), text.c_str(), text.size()) == -1) {
          editor_state->SetWarningStatus(L"Unable to paste.");
          break;
        }
      }
    } else {
      buffer->CheckPosition();
      buffer->MaybeAdjustPositionCol();
      editor_state->ApplyToCurrentBuffer(NewInsertBufferTransformation(
          it->second, editor_state->repetitions(), END));
      editor_state->ResetInsertionModifier();
    }
    editor_state->ResetRepetitions();
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
        buffer->ResetMode();
      }
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
        auto buffer = editor_state->current_buffer()->second;
        SearchOptions options;
        options.search_query = editor_state->last_search_query();
        options.starting_position = buffer->position();
        JumpToNextMatch(editor_state, options);
        buffer->ResetMode();
      }
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
        auto buffer = editor_state->current_buffer()->second;
        SearchOptions options;
        options.search_query = editor_state->last_search_query();
        options.starting_position = buffer->position();
        JumpToNextMatch(editor_state, options);
        buffer->ResetMode();
      }
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
    if (!editor_state->has_current_buffer()) { return; }
    editor_state->current_buffer()->second->set_mode(NewFindMode());
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
    if (!editor_state->has_current_buffer()) { return; }
    auto buffer = editor_state->current_buffer()->second;
    buffer->set_mode(NewRepeatMode(consumer_));
    if (c < '0' || c > '9') { return; }
    buffer->mode()->ProcessInput(c, editor_state);
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

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) { return; }
    shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    if (buffer->current_line() == nullptr) { return; }

    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr && target != buffer) {
      LOG(INFO) << "Visiting buffer: " << target->name();
      editor_state->ResetStatus();
      auto it = editor_state->buffers()->find(target->name());
      if (it == editor_state->buffers()->end()) { return; }
      editor_state->set_current_buffer(it);
      editor_state->PushCurrentPosition();
      editor_state->ScheduleRedraw();
      buffer->ResetMode();
      target->ResetMode();
      return;
    }

    buffer->MaybeAdjustPositionCol();
    wstring line = buffer->current_line()->ToString();

    const wstring& path_characters =
        buffer->read_string_variable(buffer->variable_path_characters());

    // Scroll back to the first non-path character. If we're in a non-path
    // character, this is a no-op.
    size_t start = line.find_last_not_of(
        path_characters, buffer->current_position_col());
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
  if (!editor_state->has_current_buffer()) { return; }
  auto buffer = editor_state->current_buffer()->second;
  buffer->ResetMode();
  wstring adjusted_input;
  ResolvePathOptions options;
  options.editor_state = editor_state;
  options.path = input;
  options.output_path = &adjusted_input;
  if (!ResolvePath(options)) {
    editor_state->SetWarningStatus(L"File not found: " + input);
    return;
  }

  if (editor_state->structure() == LINE) {
    auto target = buffer->GetBufferFromCurrentLine();
    if (target != nullptr) {
      buffer = target;
      target->ResetMode();
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
  SwitchCaseTransformation(CommandApplyMode apply_mode, Modifiers modifiers)
      : apply_mode_(apply_mode), modifiers_(modifiers) {}

  void Apply(EditorState* editor_state, OpenBuffer* buffer, Result* result)
      const override {
    buffer->AdjustLineColumn(&result->cursor);
    LineColumn start, end;
    if (!buffer->FindPartialRange(modifiers_, result->cursor, &start, &end)) {
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
    LineModifierSet modifiers_set =
        {LineModifier::UNDERLINE, LineModifier::BLUE};
    auto original_position = result->cursor;
    stack->PushBack(NewInsertBufferTransformation(
        buffer_to_insert,
        Modifiers(),
        modifiers_.direction == FORWARDS ? END : START,
        apply_mode_ == APPLY_PREVIEW ? &modifiers_set : nullptr));
    if (apply_mode_ == APPLY_PREVIEW) {
      stack->PushBack(NewGotoPositionTransformation(original_position));
    }
    stack->Apply(editor_state, buffer, result);
  }

  unique_ptr<Transformation> Clone() {
    return unique_ptr<Transformation>(
        new SwitchCaseTransformation(apply_mode_, modifiers_));
  }

 private:
  const CommandApplyMode apply_mode_;
  const Modifiers modifiers_;
};

void ApplySwitchCaseCommand(EditorState* editor_state, OpenBuffer* buffer,
                            CommandApplyMode apply_mode, Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  CHECK(buffer != nullptr);
  buffer->PushTransformationStack();
  buffer->ApplyToCursors(
      std::unique_ptr<SwitchCaseTransformation>(
          new SwitchCaseTransformation(apply_mode, modifiers)),
      modifiers.cursors_affected);
  buffer->PopTransformationStack();
}

}  // namespace

namespace afc {
namespace editor {

namespace {
void ToggleBoolVariable(
    EditorState* editor_state, wstring binding, wstring variable_name,
    MapMode* map_mode) {
  wstring command =
      L"// Toggle buffer variable: " + variable_name + L"\n"
      + L"Buffer tmp_buffer = CurrentBuffer();"
      + L"tmp_buffer.set_" + variable_name + L"("
      + L"!tmp_buffer." + variable_name + L"()" + L"); "
      + L"SetStatus(\"" + variable_name + L" := \" + (tmp_buffer."
      + variable_name + L"() ? \"ON\" : \"OFF\"));";
  LOG(INFO) << "Command: " << command;
  map_mode->Add(binding,
      NewCppCommand(editor_state->environment(), command).release());
}

void ToggleIntVariable(
    EditorState* editor_state, wstring binding, wstring variable_name,
    int default_value, MapMode* map_mode) {
  wstring command =
      L"// Toggle buffer variable: " + variable_name + L"\n"
      + L"Buffer tmp_buffer = CurrentBuffer();"
      + L"tmp_buffer.set_" + variable_name + L"("
      + L"tmp_buffer." + variable_name + L"() != 0 ? 0 : "
      + std::to_wstring(default_value) + L"); "
      + L"SetStatus(\"" + variable_name + L" := \" + tostring(tmp_buffer."
      + variable_name + L"()));";
  LOG(INFO) << "Command: " << command;
  map_mode->Add(binding,
      NewCppCommand(editor_state->environment(), command).release());
}
}  // namespace

using std::map;
using std::unique_ptr;

unique_ptr<EditorMode> NewCommandMode(
    EditorState* editor_state, std::shared_ptr<EditorMode> parent_mode) {
  auto map_mode = std::unique_ptr<MapMode>(new MapMode(parent_mode));
  map_mode->Add(L"aq", NewQuitCommand().release());
  map_mode->Add(L"ad", NewCloseBufferCommand().release());
  map_mode->Add(L"aw",
      NewCppCommand(editor_state->environment(),
          L"// Save the current buffer.\n"
          L"editor.SaveCurrentBuffer();").release());
  map_mode->Add(L"av", NewSetVariableCommand().release());
  map_mode->Add(L"ac", new RunCppFileCommand());
  map_mode->Add(L"aC", NewRunCppCommand().release());
  map_mode->Add(L"a.", NewOpenDirectoryCommand().release());
  map_mode->Add(L"al", NewListBuffersCommand().release());
  map_mode->Add(L"ar",
      NewCppCommand(editor_state->environment(),
          L"// Reload the current buffer.\n"
          L"editor.ReloadCurrentBuffer();").release());
  map_mode->Add(L"ae", NewSendEndOfFileCommand().release());
  map_mode->Add(L"ao", NewOpenFileCommand().release());
  {
    PromptOptions options;
    options.prompt = L"...$ ";
    options.history_file = L"commands";
    options.handler = RunMultipleCommandsHandler;
    map_mode->Add(L"aF",
        NewLinePromptCommand(
            L"forks a command for each line in the current buffer",
            [options](EditorState*) { return options; }).release());
  }
  map_mode->Add(L"af", NewForkCommand().release());

  map_mode->Add(L"+",
      NewCppCommand(editor_state->environment(),
          L"// Create a new cursor at the current position.\n"
          L"editor.CreateCursor();").release());
  map_mode->Add(L"-",
      NewCppCommand(editor_state->environment(),
          L"// Destroy current cursor(s) and jump to next.\n"
          L"editor.DestroyCursor();").release());
  map_mode->Add(L"=",
      NewCppCommand(editor_state->environment(),
          L"// Destroy cursors other than the current one.\n"
          L"editor.DestroyOtherCursors();").release());
  map_mode->Add(L"_",
      NewCppCommand(editor_state->environment(),
          L"// Toggles whether operations apply to all cursors.\n"
          L"CurrentBuffer().set_multiple_cursors(\n"
          L"    !CurrentBuffer().multiple_cursors());").release());
  map_mode->Add(L"Ct",
      NewCppCommand(editor_state->environment(),
          L"// Toggles the active cursors with the previous set.\n"
          L"editor.ToggleActiveCursors();").release());
  map_mode->Add(L"C+",
      NewCppCommand(editor_state->environment(),
          L"// Pushes the active cursors to the stack.\n"
          L"editor.PushActiveCursors();").release());
  map_mode->Add(L"C-",
      NewCppCommand(editor_state->environment(),
          L"// Pops active cursors from the stack.\n"
          L"editor.PopActiveCursors();").release());
  map_mode->Add(L"C!",
      NewCppCommand(editor_state->environment(),
          L"// Set active cursors to the marks on this buffer.\n"
          L"editor.SetActiveCursorsToMarks();").release());

  map_mode->Add(L"i", new EnterInsertModeCommand());
  map_mode->Add(L"f", new EnterFindMode());
  map_mode->Add(L"r", new ReverseDirectionCommand());
  map_mode->Add(L"R", new InsertionModifierCommand());

  map_mode->Add(L"/", NewSearchCommand().release());
  map_mode->Add(L"g", NewGotoCommand().release());

  map_mode->Add(L"w", new SetStructureCommand(WORD, L"word"));
  map_mode->Add(L"e", new SetStructureCommand(LINE, L"line"));
  map_mode->Add(L"E", new SetStructureCommand(PAGE, L"page"));
  map_mode->Add(L"F", new SetStructureCommand(SEARCH, L"search"));
  map_mode->Add(L"c", new SetStructureCommand(CURSOR, L"cursor"));
  map_mode->Add(L"B", new SetStructureCommand(BUFFER, L"buffer"));
  map_mode->Add(L"!", new SetStructureCommand(MARK, L"mark"));
  map_mode->Add(L"t", new SetStructureCommand(TREE, L"tree"));

  map_mode->Add(L"W", new SetStrengthCommand(
      Modifiers::WEAK, Modifiers::VERY_WEAK, L"weak"));
  map_mode->Add(L"S", new SetStrengthCommand(
      Modifiers::STRONG, Modifiers::VERY_STRONG, L"strong"));

  map_mode->Add(L"D", new Delete(DeleteOptions()));
  map_mode->Add(L"d",
      NewCommandWithModifiers(
              L"delete", L"starts a new delete command", ApplyDeleteCommand)
          .release());
  map_mode->Add(L"p", new Paste());

  DeleteOptions copy_options;
  copy_options.modifiers.delete_type = Modifiers::PRESERVE_CONTENTS;
  map_mode->Add(L"sc", new Delete(copy_options));
  map_mode->Add(L"u", new UndoCommand());
  map_mode->Add(L"\n", new ActivateLink());

  map_mode->Add(L"b", new GotoPreviousPositionCommand());
  map_mode->Add(L"n", NewNavigateCommand().release());
  map_mode->Add(L"j", new LineDown());
  map_mode->Add(L"k", new LineUp());
  map_mode->Add(L"l", new MoveForwards());
  map_mode->Add(L"h", new MoveBackwards());

  map_mode->Add(L"~",
      NewCommandWithModifiers(
              L"~~~~", L"Switches the case of the current character.",
              ApplySwitchCaseCommand)
          .release());

  map_mode->Add(L"sr", NewRecordCommand().release());
  map_mode->Add(L"\t", NewFindCompletionCommand().release());

  map_mode->Add(L".",
      NewCppCommand(editor_state->environment(),
          L"// Repeats the last command.\n"
          L"editor.RepeatLastTransformation();").release());

  ToggleBoolVariable(editor_state, L"vp", L"paste_mode", map_mode.get());
  ToggleBoolVariable(editor_state, L"vs", L"show_in_buffers_list",
      map_mode.get());
      
  ToggleBoolVariable(editor_state, L"vf", L"follow_end_of_file",
      map_mode.get());
  ToggleBoolVariable(editor_state, L"v/c", L"search_case_sensitive",
      map_mode.get());
  ToggleIntVariable(editor_state, L"vc", L"buffer_list_context_lines", 10,
      map_mode.get());

  map_mode->Add({Terminal::ESCAPE}, new ResetStateCommand());

  map_mode->Add(L"[", new SetStructureModifierCommand(
      Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION,
      L"from the beggining to the current position"));
  map_mode->Add(L"]", new SetStructureModifierCommand(
      Modifiers::FROM_CURRENT_POSITION_TO_END,
      L"from the current position to the end"));
  map_mode->Add({Terminal::CTRL_L}, new HardRedrawCommand());
  map_mode->Add(L"0", new NumberMode(SetRepetitions));
  map_mode->Add(L"1", new NumberMode(SetRepetitions));
  map_mode->Add(L"2", new NumberMode(SetRepetitions));
  map_mode->Add(L"3", new NumberMode(SetRepetitions));
  map_mode->Add(L"4", new NumberMode(SetRepetitions));
  map_mode->Add(L"5", new NumberMode(SetRepetitions));
  map_mode->Add(L"6", new NumberMode(SetRepetitions));
  map_mode->Add(L"7", new NumberMode(SetRepetitions));
  map_mode->Add(L"8", new NumberMode(SetRepetitions));
  map_mode->Add(L"9", new NumberMode(SetRepetitions));
  map_mode->Add({Terminal::DOWN_ARROW}, new LineDown());
  map_mode->Add({Terminal::UP_ARROW}, new LineUp());
  map_mode->Add({Terminal::LEFT_ARROW}, new MoveBackwards());
  map_mode->Add({Terminal::RIGHT_ARROW}, new MoveForwards());
  map_mode->Add({Terminal::PAGE_DOWN}, new PageDown());
  map_mode->Add({Terminal::PAGE_UP}, new PageUp());

  return std::move(map_mode);
}

}  // namespace afc
}  // namespace editor
