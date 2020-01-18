#include "src/command_with_modifiers.h"

#include <memory>

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/terminal.h"

namespace afc {
namespace editor {

bool TransformationArgumentApplyChar(wchar_t c, Modifiers* modifiers) {
  CHECK(modifiers != nullptr);
  auto set_structure = [modifiers](Structure* structure) {
    modifiers->structure =
        modifiers->structure == structure ? StructureChar() : structure;
  };

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
      if (modifiers->boundary_end == Modifiers::CURRENT_POSITION) {
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
      } else if (modifiers->boundary_end == Modifiers::LIMIT_CURRENT) {
        modifiers->boundary_end = Modifiers::LIMIT_NEIGHBOR;
      } else if (modifiers->boundary_end == Modifiers::LIMIT_NEIGHBOR) {
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
        if (modifiers->repetitions == 0) {
          modifiers->repetitions = 1;
        }
        modifiers->repetitions++;
      }
      break;

    case 'r':
      modifiers->direction = ReverseDirection(modifiers->direction);
      break;

    case 'e':
      set_structure(StructureLine());
      break;

    case 'w':
      set_structure(StructureWord());
      break;

    case 'W':
      set_structure(StructureSymbol());
      break;

    case 'B':
      set_structure(StructureBuffer());
      break;

    case 'c':
      set_structure(StructureCursor());
      break;

    case 't':
      set_structure(StructureTree());
      break;

    case 'S':
      set_structure(StructureSentence());
      break;

    case 'P':
      set_structure(StructureParagraph());
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

std::wstring TransformationArgumentBuildStatus(const Modifiers& modifiers,
                                               std::wstring name) {
  std::wstring status = name;
  if (modifiers.structure != StructureChar()) {
    status += L" " + modifiers.structure->ToString();
  }
  if (modifiers.direction == BACKWARDS) {
    status += L" reverse";
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

Modifiers::CursorsAffected TransformationArgumentCursorsAffected(
    const Modifiers& modifiers) {
  return modifiers.cursors_affected;
}

namespace {
class CommandWithModifiers : public Command {
 public:
  CommandWithModifiers(wstring name, wstring description,
                       CommandWithModifiersHandler handler)
      : name_(name), description_(description), handler_(handler) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    auto buffer = editor_state->current_buffer();
    if (buffer == nullptr) return;
    Modifiers initial_modifiers;
    initial_modifiers.cursors_affected =
        buffer->Read(buffer_variables::multiple_cursors)
            ? Modifiers::AFFECT_ALL_CURSORS
            : Modifiers::AFFECT_ONLY_CURRENT_CURSOR;
    initial_modifiers.repetitions = 0;
    buffer->set_mode(std::make_unique<TransformationArgumentMode<Modifiers>>(
        name_, editor_state, initial_modifiers, handler_));
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
