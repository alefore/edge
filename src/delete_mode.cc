#include "close_buffer_command.h"

#include <memory>

#include "editor.h"
#include "terminal.h"
#include "transformation_delete.h"

namespace afc {
namespace editor {

namespace {
class DeleteMode : public EditorMode {
 public:
  DeleteMode(EditorState* editor_state, std::shared_ptr<OpenBuffer> buffer)
      : buffer_(buffer) {
    delete_options_.modifiers.repetitions = 0;
    Apply(editor_state, INITIAL);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    switch (c) {
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
        delete_options_.modifiers.repetitions =
            10 * delete_options_.modifiers.repetitions + c - '0';
        ModifiersUpdated(editor_state);
        break;

      case 'R':
        delete_options_.modifiers.direction =
            ReverseDirection(delete_options_.modifiers.direction);
        ModifiersUpdated(editor_state);
        break;

      case 'f':
        delete_options_.modifiers.structure_range =
            delete_options_.modifiers.structure_range
                    == Modifiers::FROM_CURRENT_POSITION_TO_END
                ? Modifiers::ENTIRE_STRUCTURE
                : Modifiers::FROM_CURRENT_POSITION_TO_END;
        ModifiersUpdated(editor_state);
        break;

      case 'b':
        delete_options_.modifiers.structure_range =
            delete_options_.modifiers.structure_range
                    == Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION
                ? Modifiers::ENTIRE_STRUCTURE
                : Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
        ModifiersUpdated(editor_state);
        break;

      case 'l':
        return SetStructure(editor_state, LINE);
      case 'w':
        return SetStructure(editor_state, WORD);
      case 'B':
        return SetStructure(editor_state, BUFFER);
      case 'c':
        return SetStructure(editor_state, CURSOR);
      case 'T':
        return SetStructure(editor_state, TREE);

      case '\n':
        Apply(editor_state, FINAL);
        editor_state->ResetMode();
        editor_state->ResetStatus();
        break;

      case Terminal::ESCAPE:
        buffer_->Undo(editor_state);
        editor_state->ResetMode();
        editor_state->ResetStatus();
        break;

      default:
        ModifiersUpdated(editor_state, L"Invalid key");
    }
  }

 private:
  void SetStructure(EditorState* editor_state, Structure structure) {
    delete_options_.modifiers.structure =
        delete_options_.modifiers.structure == structure ? CHAR : structure;
    ModifiersUpdated(editor_state);
  }

  void ModifiersUpdated(EditorState* editor_state) {
    ModifiersUpdated(editor_state, L"");
  }

  void ModifiersUpdated(EditorState* editor_state, const wstring& additional) {
    CHECK(buffer_ != nullptr);
    wstring status = L"delete";
    if (delete_options_.modifiers.structure != CHAR) {
      status += L" " + StructureToString(delete_options_.modifiers.structure);
    }
    if (delete_options_.modifiers.direction == BACKWARDS) {
      status += L" reverse";
    }
    if (delete_options_.modifiers.structure_range
            == Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION) {
      status += L" backward";
    } else if (delete_options_.modifiers.structure_range
            == Modifiers::FROM_CURRENT_POSITION_TO_END) {
      status += L" forward";
    }
    if (delete_options_.modifiers.repetitions > 1) {
      status += L" " + std::to_wstring(delete_options_.modifiers.repetitions);
    }
    if (!additional.empty()) {
      status += L" - " + additional;
    }

    editor_state->SetStatus(status);
    Apply(editor_state, INTERNAL);
  }

  enum ApplyMode {
    INITIAL,  // The first call.
    INTERNAL,  // Temporary calls.
    FINAL,  // The final call.
  };

  void Apply(EditorState* editor_state, ApplyMode apply_mode) {
    if (apply_mode != INITIAL) {
      buffer_->Undo(editor_state, OpenBuffer::ONLY_UNDO_THE_LAST);
    }

    auto copy = delete_options_;
    if (copy.modifiers.repetitions == 0) {
      copy.modifiers.repetitions = 1;
    }
    copy.copy_to_paste_buffer = apply_mode == FINAL;
    buffer_->PushTransformationStack();
    buffer_->ApplyToCursors(NewDeleteTransformation(copy));
    buffer_->PopTransformationStack();
  }

  const std::shared_ptr<OpenBuffer> buffer_;
  DeleteOptions delete_options_;
  bool already_applied_ = false;
};


class DeleteCommand : public Command {
  const wstring Description() {
    return L"starts a new delete command";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->set_mode(std::unique_ptr<DeleteMode>(
        new DeleteMode(editor_state, editor_state->current_buffer()->second)));
  }

 private:
};

}  // namespace

std::unique_ptr<Command> NewDeleteCommand() {
  return unique_ptr<Command>(new DeleteCommand());
}

}  // namespace afc
}  // namespace editor
