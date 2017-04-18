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
  DeleteMode(EditorState* editor_state) {
    delete_options_.modifiers.repetitions = 0;
    DescribeModifiers(editor_state);
  }

  void ProcessInput(wint_t c, EditorState* editor_state) {
    auto buffer = editor_state->current_buffer();
    if (buffer == editor_state->buffers()->end()) {
      LOG(INFO) << "DeleteMode gives up: No current buffer.";
      editor_state->ResetMode();
      editor_state->ProcessInput(c);
      return;
    }

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
        DescribeModifiers(editor_state);
        break;

      case 'R':
        delete_options_.modifiers.direction =
            ReverseDirection(delete_options_.modifiers.direction);
        DescribeModifiers(editor_state);
        break;

      case 'f':
        delete_options_.modifiers.structure_range =
            delete_options_.modifiers.structure_range
                    == Modifiers::FROM_CURRENT_POSITION_TO_END
                ? Modifiers::ENTIRE_STRUCTURE
                : Modifiers::FROM_CURRENT_POSITION_TO_END;
        DescribeModifiers(editor_state);
        break;

      case 'b':
        delete_options_.modifiers.structure_range =
            delete_options_.modifiers.structure_range
                    == Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION
                ? Modifiers::ENTIRE_STRUCTURE
                : Modifiers::FROM_BEGINNING_TO_CURRENT_POSITION;
        DescribeModifiers(editor_state);
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
        if (delete_options_.modifiers.repetitions == 0) {
          delete_options_.modifiers.repetitions = 1;
        }
        buffer->second->ApplyToCursors(
            NewDeleteTransformation(delete_options_));
        editor_state->ResetMode();
        editor_state->ResetStatus();
        break;

      case Terminal::ESCAPE:
        editor_state->ResetMode();
        editor_state->ResetStatus();
        break;

      default:
        DescribeModifiers(editor_state, L"Invalid key");
    }
  }

 private:
  void SetStructure(EditorState* editor_state, Structure structure) {
    delete_options_.modifiers.structure =
        delete_options_.modifiers.structure == structure ? CHAR : structure;
    DescribeModifiers(editor_state);
  }

  void DescribeModifiers(EditorState* editor_state) {
    DescribeModifiers(editor_state, L"");
  }

  void DescribeModifiers(EditorState* editor_state, const wstring& additional) {
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
  }

  DeleteOptions delete_options_;
};


class DeleteCommand : public Command {
  const wstring Description() {
    return L"starts a new delete command";
  }

  void ProcessInput(wint_t, EditorState* editor_state) {
    if (!editor_state->has_current_buffer()) {
      return;
    }
    editor_state->set_mode(
        std::unique_ptr<DeleteMode>(new DeleteMode(editor_state)));
  }

 private:
};

}  // namespace

std::unique_ptr<Command> NewDeleteCommand() {
  return unique_ptr<Command>(new DeleteCommand());
}

}  // namespace afc
}  // namespace editor
