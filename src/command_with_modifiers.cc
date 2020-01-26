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
        case Modifiers::CursorsAffected::kOnlyCurrent:
          modifiers->cursors_affected = Modifiers::CursorsAffected::kAll;
          break;
        case Modifiers::CursorsAffected::kAll:
          modifiers->cursors_affected =
              Modifiers::CursorsAffected::kOnlyCurrent;
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

    case 'p':
      set_structure(StructureParagraph());
      break;

    case 'P':
      modifiers->paste_buffer_behavior =
          modifiers->paste_buffer_behavior ==
                  Modifiers::PasteBufferBehavior::kDeleteInto
              ? Modifiers::PasteBufferBehavior::kDoNothing
              : Modifiers::PasteBufferBehavior::kDeleteInto;
      break;

    case 'k':
      modifiers->delete_behavior =
          modifiers->delete_behavior == Modifiers::DeleteBehavior::kDeleteText
              ? Modifiers::DeleteBehavior::kDoNothing
              : Modifiers::DeleteBehavior::kDeleteText;
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
  if (modifiers.cursors_affected == Modifiers::CursorsAffected::kAll) {
    status += L" cursors";
  }
  if (modifiers.repetitions > 1) {
    status += L" " + std::to_wstring(modifiers.repetitions);
  }
  if (modifiers.delete_behavior == Modifiers::DeleteBehavior::kDoNothing) {
    status += L" keep";
  }
  if (modifiers.paste_buffer_behavior ==
      Modifiers::PasteBufferBehavior::kDoNothing) {
    status += L" nuke";
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
  CommandWithModifiers(wstring name, wstring description, Modifiers modifiers,
                       CommandWithModifiersHandler handler)
      : name_(name),
        description_(description),
        initial_modifiers_(std::move(modifiers)),
        handler_(handler) {}

  wstring Description() const override { return description_; }
  wstring Category() const override { return L"Edit"; }

  void ProcessInput(wint_t, EditorState* editor_state) override {
    editor_state->set_keyboard_redirect(
        std::make_unique<TransformationArgumentMode<Modifiers>>(
            name_, editor_state,
            [modifiers = initial_modifiers_](
                const std::shared_ptr<OpenBuffer>& buffer) {
              return InitialState(modifiers, buffer);
            },
            handler_));
  }

 private:
  static Modifiers InitialState(Modifiers initial_modifiers,
                                const std::shared_ptr<OpenBuffer>& buffer) {
    CHECK(buffer != nullptr);
    auto modifiers = initial_modifiers;
    modifiers.cursors_affected =
        buffer->Read(buffer_variables::multiple_cursors)
            ? Modifiers::CursorsAffected::kAll
            : Modifiers::CursorsAffected::kOnlyCurrent;
    modifiers.repetitions = 0;
    return modifiers;
  }

  const wstring name_;
  const wstring description_;
  const Modifiers initial_modifiers_;
  CommandWithModifiersHandler handler_;
};
}  // namespace

std::unique_ptr<Command> NewCommandWithModifiers(
    wstring name, wstring description, Modifiers modifiers,
    CommandWithModifiersHandler handler) {
  return std::make_unique<CommandWithModifiers>(
      std::move(name), std::move(description), std::move(modifiers),
      std::move(handler));
}

}  // namespace editor
}  // namespace afc
