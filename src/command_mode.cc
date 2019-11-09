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
#include "src/transformation_delete.h"
#include "src/transformation_move.h"
#include "src/wstring.h"

namespace afc {
namespace editor {
namespace {
using std::advance;
using std::ceil;
using std::make_pair;

class Delete : public Command {
 public:
  Delete(DeleteOptions delete_options) : delete_options_(delete_options) {}

  wstring Description() const override {
    if (delete_options_.modifiers.delete_type == Modifiers::DELETE_CONTENTS) {
      return L"deletes the current item (char, word, line...)";
    }
    return L"copies current item (char, word, ...) to the paste buffer.";
  }

  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    DeleteOptions options = delete_options_;
    options.modifiers = editor_state->modifiers();
    editor_state->ApplyToCurrentBuffer(NewDeleteTransformation(options));

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
  wstring Description() const override {
    return L"pastes the last deleted text";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    auto buffer = editor_state->current_buffer();
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
      buffer->status()->SetWarningText(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    if (it->second == editor_state->current_buffer()) {
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
      buffer->status()->SetWarningText(errors[current_message++]);
      if (errors[current_message].empty()) {
        current_message = 0;
      }
      return;
    }
    if (buffer->fd() != nullptr) {
      string text = ToByteString(it->second->ToString());
      for (size_t i = 0; i < editor_state->repetitions(); i++) {
        if (write(buffer->fd()->fd(), text.c_str(), text.size()) == -1) {
          buffer->status()->SetWarningText(L"Unable to paste.");
          break;
        }
      }
    } else {
      buffer->CheckPosition();
      buffer->MaybeAdjustPositionCol();
      InsertOptions insert_options;
      insert_options.buffer_to_insert = it->second;
      insert_options.modifiers.insertion = editor_state->modifiers().insertion;
      insert_options.modifiers.repetitions = editor_state->repetitions();
      editor_state->ApplyToCurrentBuffer(
          NewInsertBufferTransformation(std::move(insert_options)));
      editor_state->ResetInsertionModifier();
    }
    editor_state->ResetRepetitions();
  }
};

class UndoCommand : public Command {
 public:
  wstring Description() const override {
    return L"undoes the last change to the current buffer";
  }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    buffer->Undo();
    editor_state->ResetRepetitions();
    editor_state->ResetDirection();
  }
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
  auto buffer = editor_state->current_buffer();
  if (buffer == nullptr) {
    return;
  }
  // TODO: Move to Structure.
  if (structure == StructureChar()) {
    editor_state->set_structure(StructureLine());
    MoveBackwards::Move(c, editor_state);
  } else if (structure == StructureWord() || structure == StructureSymbol()) {
    // Move in whole pages.
    auto lines = editor_state->buffer_tree()->GetActiveLeaf()->size().line;
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
    auto lines = editor_state->buffer_tree()->GetActiveLeaf()->size().line;
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
  if (!editor_state->has_current_buffer()) {
    return;
  }
  editor_state->ApplyToCurrentBuffer(
      NewMoveTransformation(editor_state->modifiers()));
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
  if (!editor_state->has_current_buffer()) {
    return;
  }
  editor_state->set_direction(ReverseDirection(editor_state->direction()));
  MoveForwards::Move(c, editor_state);
  return;
}

class EnterInsertModeCommand : public Command {
 public:
  wstring Description() const override { return L"enters insert mode"; }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    EnterInsertMode(editor_state);
  }
};

class EnterFindMode : public Command {
 public:
  wstring Description() const override {
    return L"Waits for a character to be typed and moves the cursor to its "
           L"next occurrence in the current line.";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    buffer->set_mode(NewFindMode());
  }
};

class InsertionModifierCommand : public Command {
 public:
  wstring Description() const override {
    return L"activates replace modifier (overwrites text on insertions)";
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (editor_state->insertion_modifier() == Modifiers::INSERT) {
      editor_state->set_insertion_modifier(Modifiers::REPLACE);
    } else if (editor_state->default_insertion_modifier() ==
               Modifiers::INSERT) {
      editor_state->set_default_insertion_modifier(Modifiers::REPLACE);
    } else {
      editor_state->set_default_insertion_modifier(Modifiers::INSERT);
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

void SetRepetitions(EditorState* editor_state, int number) {
  editor_state->set_repetitions(number);
}

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

class SetStructureModifierCommand : public Command {
 public:
  SetStructureModifierCommand(Modifiers::StructureRange value,
                              const wstring& description)
      : value_(value), description_(description) {}

  wstring Description() const override {
    return L"sets the structure modifier: " + description_;
  }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->set_structure_range(editor_state->structure_range() == value_
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

  NumberMode(const wstring& description,
             function<void(EditorState*, int)> consumer)
      : description_(description), consumer_(consumer) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Modifiers"; }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) {
      return;
    }
    buffer->set_mode(NewRepeatMode(consumer_));
    if (c < '0' || c > '9') {
      return;
    }
    buffer->mode()->ProcessInput(c, editor_state);
  }

 private:
  const wstring description_;
  function<void(EditorState*, int)> consumer_;
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
          target_position->type.type == VMType::OBJECT_TYPE &&
          target_position->type.object_type == L"LineColumn") {
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

class SwitchCaseTransformation : public Transformation {
 public:
  SwitchCaseTransformation(Modifiers modifiers) : modifiers_(modifiers) {}

  void Apply(OpenBuffer* buffer, Result* result) const override {
    buffer->AdjustLineColumn(&result->cursor);
    Range range = buffer->FindPartialRange(modifiers_, result->cursor);
    CHECK_LE(range.begin, range.end);
    TransformationStack stack;
    stack.PushBack(NewGotoPositionTransformation(range.begin));
    auto buffer_to_insert =
        std::make_shared<OpenBuffer>(buffer->editor(), L"- text inserted");
    VLOG(5) << "Switch Case Transformation at " << result->cursor << ": "
            << buffer->editor()->modifiers() << ": Range: " << range;
    LineColumn i = range.begin;
    while (i < range.end) {
      auto line = buffer->LineAt(i.line);
      if (line == nullptr) {
        break;
      }
      if (i.column >= line->EndColumn()) {
        // Switch to the next line.
        i = LineColumn(i.line + LineNumberDelta(1));
        DeleteOptions options;
        options.copy_to_paste_buffer = false;
        stack.PushBack(std::make_unique<TransformationWithMode>(
            Transformation::Result::Mode::kFinal,
            NewDeleteTransformation(options)));
        buffer_to_insert->AppendEmptyLine();
        continue;
      }
      wchar_t c = line->get(i.column);
      buffer_to_insert->AppendToLastLine(
          NewLazyString(wstring(1, iswupper(c) ? towlower(c) : towupper(c))));
      DeleteOptions options;
      options.copy_to_paste_buffer = false;
      stack.PushBack(std::make_unique<TransformationWithMode>(
          Transformation::Result::Mode::kFinal,
          NewDeleteTransformation(options)));

      // Increment i.
      i.column++;
    }
    auto original_position = result->cursor;
    InsertOptions insert_options;
    insert_options.buffer_to_insert = buffer_to_insert;
    if (modifiers_.direction == BACKWARDS) {
      insert_options.final_position = InsertOptions::FinalPosition::kStart;
    }
    if (result->mode == Transformation::Result::Mode::kPreview) {
      insert_options.modifiers_set = {LineModifier::UNDERLINE,
                                      LineModifier::BLUE};
    }
    stack.PushBack(NewInsertBufferTransformation(std::move(insert_options)));
    if (result->mode == Transformation::Result::Mode::kPreview) {
      stack.PushBack(NewGotoPositionTransformation(original_position));
    }
    stack.Apply(buffer, result);
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<SwitchCaseTransformation>(modifiers_);
  }

 private:
  const Modifiers modifiers_;
};

std::unique_ptr<Transformation> ApplySwitchCaseCommand(
    EditorState* editor_state, OpenBuffer* buffer, Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  CHECK(buffer != nullptr);
  return std::make_unique<SwitchCaseTransformation>(modifiers);
}

class TreeNavigate : public Transformation {
  void Apply(OpenBuffer* buffer, Result* result) const override {
    auto root = buffer->parse_tree();
    if (root == nullptr) {
      result->success = false;
      return;
    }
    const ParseTree* tree = root.get();
    auto next_position = result->cursor;
    Seek(*buffer->contents(), &next_position).Once();
    while (true) {
      size_t child = 0;
      while (child < tree->children().size() &&
             (tree->children()[child].range().end <= result->cursor ||
              tree->children()[child].children().empty())) {
        child++;
      }
      if (child < tree->children().size()) {
        bool descend = false;
        auto candidate = &tree->children()[child];
        if (tree->range().begin < result->cursor) {
          descend = true;
        } else if (tree->range().end == next_position) {
          descend = candidate->range().end == next_position;
        }

        if (descend) {
          tree = candidate;
          continue;
        }
      }

      auto last_position = tree->range().end;
      Seek(*buffer->contents(), &last_position).Backwards().Once();

      auto original_cursor = result->cursor;
      result->cursor = result->cursor < tree->range().begin ||
                               result->cursor == last_position
                           ? tree->range().begin
                           : last_position;
      result->success = original_cursor != result->cursor;
      return;
    }
  }

  std::unique_ptr<Transformation> Clone() const override {
    return std::make_unique<TreeNavigate>();
  }
};

class TreeNavigateCommand : public Command {
 public:
  wstring Description() const override {
    return L"Navigates to the start/end of the current children of the "
           L"syntax "
           L"tree";
  }
  wstring Category() const override { return L"Navigate"; }

  void ProcessInput(wint_t, EditorState* editor_state) {
    editor_state->ApplyToCurrentBuffer(std::make_unique<TreeNavigate>());
  }
};

void ToggleBoolVariable(EditorState* editor_state, wstring binding,
                        wstring variable_name, MapModeCommands* map_mode) {
  wstring command = L"// Variables: Toggle buffer variable: " + variable_name +
                    L"\nBuffer tmp_buffer = CurrentBuffer(); tmp_buffer.set_" +
                    variable_name + L"(!tmp_buffer." + variable_name +
                    L"()); SetStatus((tmp_buffer." + variable_name +
                    L"() ? \"🗸\" : \"⛶\") + \" " + variable_name + L"\");";
  LOG(INFO) << "Command: " << command;
  map_mode->Add(binding, NewCppCommand(editor_state->environment(), command));
  map_mode->RegisterVariableCommand(variable_name, binding);
}

void ToggleIntVariable(EditorState* editor_state, wstring binding, wstring name,
                       MapModeCommands* map_mode) {
  wstring command = L"// Variables: Toggle buffer variable: " + name +
                    L"\nBuffer tmp_buffer = CurrentBuffer();" +
                    L"tmp_buffer.set_" + name +
                    L"(repetitions());set_repetitions(1);SetStatus(\"" + name +
                    L" := \" + tmp_buffer." + name + L"().tostring());";
  LOG(INFO) << "Command: " << command;
  map_mode->Add(binding, NewCppCommand(editor_state->environment(), command));
  map_mode->RegisterVariableCommand(name, binding);
}
}  // namespace

using std::map;
using std::unique_ptr;

std::unique_ptr<MapModeCommands> NewCommandMode(EditorState* editor_state) {
  auto commands = std::make_unique<MapModeCommands>();
  commands->Add(L"aq", NewQuitCommand(0));
  commands->Add(L"aQ", NewQuitCommand(1));
  commands->Add(L"ad", NewCloseBufferCommand());
  commands->Add(L"aw", NewCppCommand(editor_state->environment(),
                                     L"// Buffers: Save the current buffer.\n"
                                     L"editor.SaveCurrentBuffer();"));
  commands->Add(L"av", NewSetVariableCommand());
  commands->Add(L"ac", NewRunCppFileCommand());
  commands->Add(L"aC", NewRunCppCommand());
  commands->Add(L"a.", NewOpenDirectoryCommand());
  commands->Add(L"aL", NewListBuffersCommand());
  commands->Add(L"ar", NewCppCommand(editor_state->environment(),
                                     L"// Buffers: Reload the current buffer.\n"
                                     L"editor.ReloadCurrentBuffer();"));
  commands->Add(L"ae", NewSendEndOfFileCommand());
  commands->Add(L"ao", NewOpenFileCommand());
  {
    PromptOptions options;
    options.prompt = L"...$ ";
    options.history_file = L"commands";
    options.handler = RunMultipleCommandsHandler;
    commands->Add(L"aF",
                  NewLinePromptCommand(
                      L"forks a command for each line in the current buffer",
                      [options](EditorState*) { return options; }));
  }
  commands->Add(L"af", NewForkCommand());

  commands->Add(
      L"+", NewCppCommand(
                editor_state->environment(),
                L"// Cursors: Create a new cursor at the current position.\n"
                L"editor.CreateCursor();"));
  commands->Add(
      L"-",
      NewCppCommand(editor_state->environment(),
                    L"// Cursors: Destroy current cursor(s) and jump to next.\n"
                    L"editor.DestroyCursor();"));
  commands->Add(
      L"=",
      NewCppCommand(editor_state->environment(),
                    L"// Cursors: Destroy cursors other than the current one.\n"
                    L"editor.DestroyOtherCursors();"));
  commands->Add(
      L"_",
      NewCppCommand(
          editor_state->environment(),
          L"// Cursors: Toggles whether operations apply to all cursors.\n"
          L"CurrentBuffer().set_multiple_cursors(\n"
          L"    !CurrentBuffer().multiple_cursors());"));
  commands->Add(
      L"Ct",
      NewCppCommand(
          editor_state->environment(),
          L"// Cursors: Toggles the active cursors with the previous set.\n"
          L"editor.ToggleActiveCursors();"));
  commands->Add(
      L"C+",
      NewCppCommand(editor_state->environment(),
                    L"// Cursors: Pushes the active cursors to the stack.\n"
                    L"editor.PushActiveCursors();"));
  commands->Add(
      L"C-", NewCppCommand(editor_state->environment(),
                           L"// Cursors: Pops active cursors from the stack.\n"
                           L"editor.PopActiveCursors();"));
  commands->Add(
      L"C!",
      NewCppCommand(
          editor_state->environment(),
          L"// Cursors: Set active cursors to the marks on this buffer.\n"
          L"editor.SetActiveCursorsToMarks();"));

  commands->Add(L"N", NewNavigationBufferCommand());
  commands->Add(L"i", std::make_unique<EnterInsertModeCommand>());
  commands->Add(L"f", std::make_unique<EnterFindMode>());
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

  commands->Add(L"D", std::make_unique<Delete>(DeleteOptions()));
  commands->Add(L"d",
                NewCommandWithModifiers(L"✀ ", L"starts a new delete command",
                                        ApplyDeleteCommand));
  commands->Add(L"p", std::make_unique<Paste>());

  DeleteOptions copy_options;
  copy_options.modifiers.delete_type = Modifiers::PRESERVE_CONTENTS;
  commands->Add(L"u", std::make_unique<UndoCommand>());
  commands->Add(L"\n", std::make_unique<ActivateLink>());

  commands->Add(L"b", std::make_unique<GotoPreviousPositionCommand>());
  commands->Add(L"n", NewNavigateCommand());
  commands->Add(L"j", std::make_unique<LineDown>());
  commands->Add(L"k", std::make_unique<LineUp>());
  commands->Add(L"l", std::make_unique<MoveForwards>());
  commands->Add(L"h", std::make_unique<MoveBackwards>());

  commands->Add(L"~", NewCommandWithModifiers(
                          L"🔠🔡", L"Switches the case of the current character.",
                          ApplySwitchCaseCommand));

  commands->Add(L"%", std::make_unique<TreeNavigateCommand>());
  commands->Add(L"sr", NewRecordCommand());
  commands->Add(L"\t", NewFindCompletionCommand());

  commands->Add(L".", NewCppCommand(editor_state->environment(),
                                    L"// Edit: Repeats the last command.\n"
                                    L"editor.RepeatLastTransformation();"));

  ToggleBoolVariable(editor_state, L"vp", L"paste_mode", commands.get());
  ToggleBoolVariable(editor_state, L"vS", L"scrollbar", commands.get());
  ToggleBoolVariable(editor_state, L"vW", L"wrap_long_lines", commands.get());
  ToggleBoolVariable(editor_state, L"vs", L"show_in_buffers_list",
                     commands.get());

  ToggleBoolVariable(editor_state, L"vf", L"follow_end_of_file",
                     commands.get());
  ToggleBoolVariable(editor_state, L"v/c", L"search_case_sensitive",
                     commands.get());
  ToggleIntVariable(editor_state, L"vc", L"buffer_list_context_lines",
                    commands.get());
  ToggleIntVariable(editor_state, L"vw", L"line_width", commands.get());

  commands->Add({Terminal::ESCAPE}, std::make_unique<ResetStateCommand>());

  commands->Add(L"[", std::make_unique<SetStructureModifierCommand>(
                          Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION,
                          L"from the beggining to the current position"));
  commands->Add(L"]", std::make_unique<SetStructureModifierCommand>(
                          Modifiers::FROM_CURRENT_POSITION_TO_END,
                          L"from the current position to the end"));
  commands->Add({Terminal::CTRL_L}, std::make_unique<HardRedrawCommand>());
  commands->Add(L"*", std::make_unique<SetStrengthCommand>());
  commands->Add(L"0", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"1", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"2", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"3", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"4", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"5", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"6", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"7", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"8", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add(L"9", std::make_unique<NumberMode>(SetRepetitions));
  commands->Add({Terminal::CTRL_E},
                NewCppCommand(editor_state->environment(),
                              L"// Navigate: Move to the end of line.\n"
                              L"CurrentBuffer().ApplyTransformation("
                              L"TransformationGoToColumn(999999999999));"));
  commands->Add({Terminal::CTRL_A},
                NewCppCommand(editor_state->environment(),
                              L"// Navigate: Move to the beginning of line.\n"
                              L"CurrentBuffer().ApplyTransformation("
                              L"TransformationGoToColumn(0));"));
  commands->Add({Terminal::CTRL_K},
                NewCppCommand(editor_state->environment(),
                              L"// Edit: Delete to end of line.\n"
                              L"{\n"
                              L"Modifiers modifiers = Modifiers();\n"
                              L"modifiers.set_line();\n"
                              L"CurrentBuffer().ApplyTransformation("
                              L"TransformationDelete(modifiers));\n"
                              L"}"));
  commands->Add({Terminal::CTRL_U},
                NewCppCommand(editor_state->environment(),
                              L"// Edit: Delete to the beginning of line.\n"
                              L"{\n"
                              L"Modifiers modifiers = Modifiers();\n"
                              L"modifiers.set_line();\n"
                              L"modifiers.set_backwards();\n"
                              L"CurrentBuffer().ApplyTransformation("
                              L"TransformationDelete(modifiers));\n"
                              L"}"));
  commands->Add({Terminal::CTRL_D},
                NewCppCommand(editor_state->environment(),
                              L"// Edit: Delete current character.\n"
                              L"CurrentBuffer().ApplyTransformation("
                              L"TransformationDelete(Modifiers()));\n"));
  commands->Add({Terminal::BACKSPACE},
                NewCppCommand(editor_state->environment(),
                              L"// Edit: Delete previous character.\n"
                              L"{\n"
                              L"Modifiers modifiers = Modifiers();\n"
                              L"modifiers.set_backwards();\n"
                              L"CurrentBuffer().ApplyTransformation("
                              L"TransformationDelete(modifiers));\n"
                              L"}"));
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
