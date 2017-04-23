#include "delete_mode.h"

#include <memory>

#include "editor.h"
#include "terminal.h"
#include "transformation_delete.h"

namespace afc {
namespace editor {

namespace {
enum ApplyMode {
  INITIAL,  // The first call.
  INTERNAL,  // Temporary calls.
  FINAL,  // The final call.
  CANCEL,  // Cancel the whole operation.
};

class CommandWithModifiers : public EditorMode {
 public:
  using Callback = std::function<void(ApplyMode, Modifiers modifiers)>;

  CommandWithModifiers(wstring name,
                       EditorState* editor_state,
                       Callback callback)
      : name_(std::move(name)),
        callback_(std::move(callback)) {
    RunCallback(editor_state, INITIAL);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (c) {
      case '\n':
      case ' ':
        RunCallback(editor_state, FINAL);
        break;

      case Terminal::ESCAPE:
        RunCallback(editor_state, CANCEL);
        break;

      case Terminal::BACKSPACE:
        if (!modifiers_string_.empty()) {
          modifiers_string_.pop_back();
        }
        RunCallback(editor_state, INTERNAL);
        break;

      default:
        modifiers_string_.push_back(c);
        RunCallback(editor_state, INTERNAL);
    }
  }

 private:
  void RunCallback(EditorState* editor_state, ApplyMode apply_mode) {
    callback_(apply_mode, BuildModifiers(editor_state));
  }

  Modifiers BuildModifiers(EditorState* editor_state) {
    Modifiers modifiers;
    wstring additional_information;
    modifiers.repetitions = 0;
    for (const auto& c : modifiers_string_) {
      additional_information = L"";
      switch (c) {
        case '+':
          if (modifiers.repetitions == 0) {
            modifiers.repetitions = 1;
          }
          modifiers.repetitions++;
          break;

        case '-':
          if (modifiers.repetitions > 0) {
            modifiers.repetitions--;
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
          modifiers.repetitions = 10 * modifiers.repetitions + c - '0';
          break;

        case 'k':
          modifiers.boundary_begin = Modifiers::CURRENT_POSITION;
          modifiers.boundary_end = Modifiers::CURRENT_POSITION;
          break;

        case 'j':
          modifiers.boundary_begin = Modifiers::LIMIT_CURRENT;
          modifiers.boundary_end = Modifiers::LIMIT_NEIGHBOR;
          break;

        case 'h':
          modifiers.boundary_begin =
              IncrementBoundary(modifiers.boundary_begin);
          break;

        case 'l':
          modifiers.boundary_end = IncrementBoundary(modifiers.boundary_end);
          break;

        case 'r':
          modifiers.direction = ReverseDirection(modifiers.direction);
          break;

        case 'f':
          modifiers.structure_range =
              modifiers.structure_range
                      == Modifiers::FROM_CURRENT_POSITION_TO_END
                  ? Modifiers::ENTIRE_STRUCTURE
                  : Modifiers::FROM_CURRENT_POSITION_TO_END;
          break;

        case 'b':
          modifiers.structure_range =
              modifiers.structure_range
                      == Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION
                  ? Modifiers::ENTIRE_STRUCTURE
                  : Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
          break;

        case 'e':
          SetStructure(LINE, &modifiers);
          break;

        case 'w':
          SetStructure(WORD, &modifiers);
          break;

        case 'B':
          SetStructure(BUFFER, &modifiers);
          break;

        case 'c':
          SetStructure(CURSOR, &modifiers);
          break;

        case 'T':
          SetStructure(TREE, &modifiers);
          break;

        case 'p':
          modifiers.delete_type =
              modifiers.delete_type == Modifiers::DELETE_CONTENTS
                  ? Modifiers::PRESERVE_CONTENTS : Modifiers::DELETE_CONTENTS;
          break;

        default:
          additional_information = L"Invalid key: " + wstring(1, c);
      }
    }
    if (modifiers.repetitions == 0) {
      modifiers.repetitions = 1;
    }
    UpdateStatus(editor_state, modifiers, additional_information);
    return modifiers;
  }

  void SetStructure(Structure structure, Modifiers* modifiers) {
    modifiers->structure = modifiers->structure == structure ? CHAR : structure;
  }

  void UpdateStatus(EditorState* editor_state, const Modifiers& modifiers,
                    const wstring& additional_information) {
    wstring status = name_;
    if (modifiers.structure != CHAR) {
      status += L" " + StructureToString(modifiers.structure);
    }
    if (modifiers.direction == BACKWARDS) {
      status += L" reverse";
    }
    if (modifiers.structure_range
            == Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION) {
      status += L" backward";
    } else if (modifiers.structure_range
            == Modifiers::FROM_CURRENT_POSITION_TO_END) {
      status += L" forward";
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

    if (!additional_information.empty()) {
      status += L" [" + additional_information + L"]";
    }
    editor_state->SetStatus(status);
  }

  const wstring name_;
  const Callback callback_;
  wstring modifiers_string_;
};

void ApplyDelete(EditorState* editor_state, OpenBuffer* buffer,
                 ApplyMode apply_mode, Modifiers modifiers) {
  CHECK(editor_state != nullptr);
  CHECK(buffer != nullptr);
  if (apply_mode != INITIAL) {
    buffer->Undo(editor_state, OpenBuffer::ONLY_UNDO_THE_LAST);
  }

  if (apply_mode != CANCEL) {
    DeleteOptions options;
    options.modifiers = modifiers;
    options.copy_to_paste_buffer = apply_mode == FINAL;
    options.preview = apply_mode != FINAL;

    buffer->PushTransformationStack();
    buffer->ApplyToCursors(NewDeleteTransformation(options));
    buffer->PopTransformationStack();
  }

  if (apply_mode == CANCEL || apply_mode == FINAL) {
    editor_state->ResetMode();
    editor_state->ResetStatus();
  }
}

class DeleteCommand : public Command {
  const wstring Description() {
    return L"starts a new delete command";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    std::shared_ptr<OpenBuffer> buffer = editor_state->current_buffer()->second;
    editor_state->set_mode(std::unique_ptr<CommandWithModifiers>(
        new CommandWithModifiers(
            L"delete", editor_state,
            [editor_state, buffer](ApplyMode apply_mode, Modifiers modifiers) {
              ApplyDelete(editor_state, buffer.get(), apply_mode, modifiers);
            })));
  }

 private:
};

}  // namespace

std::unique_ptr<Command> NewDeleteCommand() {
  return unique_ptr<Command>(new DeleteCommand());
}

}  // namespace afc
}  // namespace editor
