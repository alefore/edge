#include "command_with_modifiers.h"

#include <memory>

#include "buffer_variables.h"
#include "editor.h"
#include "terminal.h"

namespace afc {
namespace editor {

namespace {
class CommandWithModifiersMode : public EditorMode {
 public:
  CommandWithModifiersMode(wstring name, EditorState* editor_state,
                           CommandWithModifiersHandler handler)
      : name_(std::move(name)),
        buffer_(editor_state->current_buffer()->second),
        handler_(std::move(handler)) {
    CHECK(buffer_ != nullptr);
    RunHandler(editor_state, Transformation::Result::Mode::kPreview);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) override {
    buffer_->Undo(editor_state, OpenBuffer::ONLY_UNDO_THE_LAST);
    switch (static_cast<int>(c)) {
      case Terminal::ESCAPE:
        buffer_->ResetMode();
        editor_state->ResetStatus();
        break;

      case Terminal::BACKSPACE:
        if (!modifiers_string_.empty()) {
          modifiers_string_.pop_back();
        }
        RunHandler(editor_state, Transformation::Result::Mode::kPreview);
        break;

      default:
        if (!ApplyChar(c, nullptr)) {
          RunHandler(editor_state, Transformation::Result::Mode::kFinal);
          buffer_->ResetMode();
          editor_state->ResetStatus();
          if (c != L'\n') {
            editor_state->ProcessInput(c);
          }
        } else {
          modifiers_string_.push_back(c);
          RunHandler(editor_state, Transformation::Result::Mode::kPreview);
        }
    }
  }

 private:
  void RunHandler(EditorState* editor_state,
                  Transformation::Result::Mode apply_mode) {
    Modifiers modifiers = BuildModifiers(editor_state);
    buffer_->ApplyToCursors(handler_(editor_state, buffer_.get(), modifiers),
                            modifiers.cursors_affected, apply_mode);
  }

  Modifiers BuildModifiers(EditorState* editor_state) {
    Modifiers modifiers;
    modifiers.cursors_affected =
        buffer_->Read(buffer_variables::multiple_cursors())
            ? Modifiers::AFFECT_ALL_CURSORS
            : Modifiers::AFFECT_ONLY_CURRENT_CURSOR;
    modifiers.repetitions = 0;
    for (const auto& c : modifiers_string_) {
      ApplyChar(c, &modifiers);
    }
    if (modifiers.repetitions == 0) {
      modifiers.repetitions = 1;
    }
    editor_state->SetStatus(BuildStatus(modifiers));
    return modifiers;
  }

  static bool ApplyChar(wchar_t c, Modifiers* modifiers) {
    Modifiers dummy;
    if (modifiers == nullptr) {
      modifiers = &dummy;
    }
    switch (c) {
      case '+':
        if (modifiers->repetitions == 0) {
          modifiers->repetitions = 1;
        }
        modifiers->repetitions++;
        break;

      case '-':
        if (modifiers->repetitions > 0) {
          modifiers->repetitions--;
        }
        break;

      case '*':
        switch (modifiers->cursors_affected) {
          case Modifiers::AFFECT_ONLY_CURRENT_CURSOR:
            modifiers->cursors_affected = Modifiers::AFFECT_ALL_CURSORS;
            break;
          case Modifiers::AFFECT_ALL_CURSORS:
            modifiers->cursors_affected = Modifiers::AFFECT_ONLY_CURRENT_CURSOR;
            break;
        }
        break;

      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        modifiers->repetitions = 10 * modifiers->repetitions + c - '0';
        break;

      case '(':
        modifiers->boundary_begin = Modifiers::CURRENT_POSITION;
        break;

      case '[':
        modifiers->boundary_begin = Modifiers::LIMIT_CURRENT;
        break;

      case '{':
        modifiers->boundary_begin = Modifiers::LIMIT_NEIGHBOR;
        break;

      case ')':
        modifiers->boundary_end = Modifiers::CURRENT_POSITION;
        break;

      case ']':
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
        break;

      case '}':
        modifiers->boundary_end = Modifiers::LIMIT_NEIGHBOR;
        break;

      case 'r':
        modifiers->direction = ReverseDirection(modifiers->direction);
        break;

      case 'f':
        modifiers->structure_range =
            modifiers->structure_range ==
                    Modifiers::FROM_CURRENT_POSITION_TO_END
                ? Modifiers::ENTIRE_STRUCTURE
                : Modifiers::FROM_CURRENT_POSITION_TO_END;
        break;

      case 'b':
        modifiers->structure_range =
            modifiers->structure_range ==
                    Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION
                ? Modifiers::ENTIRE_STRUCTURE
                : Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
        break;

      case 'e':
        SetStructure(LINE, modifiers);
        break;

      case 'w':
        SetStructure(WORD, modifiers);
        break;

      case 'W':
        SetStructure(SYMBOL, modifiers);
        break;

      case 'B':
        SetStructure(BUFFER, modifiers);
        break;

      case 'c':
        SetStructure(CURSOR, modifiers);
        break;

      case 'T':
        SetStructure(TREE, modifiers);
        break;

      case 'P':
        SetStructure(PARAGRAPH, modifiers);
        break;

      case 'p':
        modifiers->delete_type =
            modifiers->delete_type == Modifiers::DELETE_CONTENTS
                ? Modifiers::PRESERVE_CONTENTS
                : Modifiers::DELETE_CONTENTS;
        break;

      default:
        return false;
    }
    return true;
  }

  static void SetStructure(Structure structure, Modifiers* modifiers) {
    modifiers->structure = modifiers->structure == structure ? CHAR : structure;
  }

  wstring BuildStatus(const Modifiers& modifiers) {
    wstring status = name_;
    if (modifiers.structure != CHAR) {
      status += L" " + StructureToString(modifiers.structure);
    }
    if (modifiers.direction == BACKWARDS) {
      status += L" reverse";
    }
    if (modifiers.structure_range ==
        Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION) {
      status += L" backward";
    } else if (modifiers.structure_range ==
               Modifiers::FROM_CURRENT_POSITION_TO_END) {
      status += L" forward";
    }
    if (modifiers.cursors_affected == Modifiers::AFFECT_ALL_CURSORS) {
      status += L" cursors";
    }
    if (modifiers.repetitions > 1) {
      status += L" " + std::to_wstring(modifiers.repetitions);
    }
    if (modifiers.delete_type == Modifiers::PRESERVE_CONTENTS) {
      status += L" preserve";
    }

    status += L" ";
    switch (modifiers.boundary_begin) {
      case Modifiers::LIMIT_NEIGHBOR:
        status += L"<";
        break;
      case Modifiers::LIMIT_CURRENT:
        status += L"(";
        break;
      case Modifiers::CURRENT_POSITION:
        status += L"[";
    }
    switch (modifiers.boundary_end) {
      case Modifiers::LIMIT_NEIGHBOR:
        status += L">";
        break;
      case Modifiers::LIMIT_CURRENT:
        status += L")";
        break;
      case Modifiers::CURRENT_POSITION:
        status += L"]";
    }

    return status;
  }

  const wstring name_;
  const std::shared_ptr<OpenBuffer> buffer_;
  const CommandWithModifiersHandler handler_;
  wstring modifiers_string_;
};

class CommandWithModifiers : public Command {
 public:
  CommandWithModifiers(wstring name, wstring description,
                       CommandWithModifiersHandler handler)
      : name_(name), description_(description), handler_(handler) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    if (editor_state->has_current_buffer()) {
      editor_state->current_buffer()->second->set_mode(
          std::make_unique<CommandWithModifiersMode>(name_, editor_state,
                                                     handler_));
    }
  }

 private:
  const wstring name_;
  const wstring description_;
  CommandWithModifiersHandler handler_;
};

}  // namespace

std::unique_ptr<Command> NewCommandWithModifiers(
    wstring name, wstring description, CommandWithModifiersHandler handler) {
  return std::make_unique<CommandWithModifiers>(
      std::move(name), std::move(description), std::move(handler));
}

}  // namespace editor
}  // namespace afc
