#include "src/command_with_modifiers.h"

#include <memory>

#include "src/buffer_variables.h"
#include "src/command_argument_mode.h"
#include "src/editor.h"
#include "src/set_mode_command.h"
#include "src/terminal.h"

namespace afc::editor {
namespace {
bool CharConsumer(wint_t c, Modifiers* modifiers) {
  auto set_structure = [modifiers](Structure* structure) {
    modifiers->structure =
        modifiers->structure == structure ? StructureChar() : structure;
  };

  switch (c) {
    case '+':
      modifiers->repetitions = modifiers->repetitions.value_or(1) + 1;
      return true;

    case '-':
      if (modifiers->repetitions.value_or(1) > 0) {
        modifiers->repetitions = modifiers->repetitions.value_or(1) - 1;
      }
      return true;

    case '*':
      switch (modifiers->cursors_affected.value_or(
          Modifiers::kDefaultCursorsAffected)) {
        case Modifiers::CursorsAffected::kOnlyCurrent:
          modifiers->cursors_affected = Modifiers::CursorsAffected::kAll;
          break;
        case Modifiers::CursorsAffected::kAll:
          modifiers->cursors_affected =
              Modifiers::CursorsAffected::kOnlyCurrent;
          break;
      }
      return true;

    case L'0':
    case L'1':
    case L'2':
    case L'3':
    case L'4':
    case L'5':
    case L'6':
    case L'7':
    case L'8':
    case L'9':
      modifiers->repetitions =
          10 * modifiers->repetitions.value_or(0) + c - L'0';
      return true;

    case '(':
      modifiers->boundary_begin = Modifiers::CURRENT_POSITION;
      return true;

    case '[':
      modifiers->boundary_begin = Modifiers::LIMIT_CURRENT;
      return true;

    case '{':
      modifiers->boundary_begin = Modifiers::LIMIT_NEIGHBOR;
      return true;

    case ')':
      modifiers->boundary_end = Modifiers::CURRENT_POSITION;
      return true;

    case ']':
      if (modifiers->boundary_end == Modifiers::CURRENT_POSITION) {
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
      } else if (modifiers->boundary_end == Modifiers::LIMIT_CURRENT) {
        modifiers->boundary_end = Modifiers::LIMIT_NEIGHBOR;
      } else if (modifiers->boundary_end == Modifiers::LIMIT_NEIGHBOR) {
        modifiers->boundary_end = Modifiers::LIMIT_CURRENT;
        if (!modifiers->repetitions.has_value()) {
          modifiers->repetitions = 1;
        }
        ++modifiers->repetitions.value();
      }
      return true;

    case 'r':
      modifiers->direction = ReverseDirection(modifiers->direction);
      return true;

    case 'e':
      set_structure(StructureLine());
      return true;

    case 'w':
      set_structure(StructureWord());
      return true;

    case 'W':
      set_structure(StructureSymbol());
      return true;

    case 'B':
      set_structure(StructureBuffer());
      return true;

    case 'c':
      set_structure(StructureCursor());
      return true;

    case 't':
      set_structure(StructureTree());
      return true;

    case 'S':
      set_structure(StructureSentence());
      return true;

    case 'p':
      set_structure(StructureParagraph());
      return true;

    case 'P':
      modifiers->paste_buffer_behavior =
          modifiers->paste_buffer_behavior ==
                  Modifiers::PasteBufferBehavior::kDeleteInto
              ? Modifiers::PasteBufferBehavior::kDoNothing
              : Modifiers::PasteBufferBehavior::kDeleteInto;
      return true;

    case 'k':
      modifiers->delete_behavior =
          modifiers->delete_behavior == Modifiers::DeleteBehavior::kDeleteText
              ? Modifiers::DeleteBehavior::kDoNothing
              : Modifiers::DeleteBehavior::kDeleteText;
      return modifiers;

    default:
      return false;
  }
}

std::wstring BuildStatus(
    const std::function<std::wstring(const Modifiers&)>& name,
    const Modifiers& modifiers) {
  std::wstring status = name(modifiers);
  if (modifiers.structure != StructureChar()) {
    status += L" " + modifiers.structure->ToString();
  }
  if (modifiers.direction == Direction::kBackwards) {
    status += L" reverse";
  }
  if (modifiers.cursors_affected == Modifiers::CursorsAffected::kAll) {
    status += L" multiple_cursors";
  }
  if (modifiers.repetitions.has_value()) {
    status += L" " + std::to_wstring(modifiers.repetitions.value());
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
}  // namespace

std::unique_ptr<Command> NewCommandWithModifiers(
    std::function<std::wstring(const Modifiers&)> name_function,
    wstring description, Modifiers modifiers,
    CommandWithModifiersHandler handler, EditorState* editor_state) {
  return NewSetModeCommand(
      {.editor_state = *editor_state,
       .description = description,
       .category = L"Edit",
       .factory = [editor_state, name_function, modifiers,
                   handler = std::move(handler)] {
         Modifiers mutable_modifiers = modifiers;
         // TODO: Find a way to have this honor `multiple_cursors`. Perhaps the
         // best way is to get rid of that? Or somehow merge that with
         // Modifiers.cursors_affected.
         if (editor_state->modifiers().cursors_affected.has_value()) {
           mutable_modifiers.cursors_affected =
               editor_state->modifiers().cursors_affected;
         }
         CommandArgumentMode<Modifiers>::Options options{
             .editor_state = *editor_state,
             .initial_value = std::move(mutable_modifiers),
             .char_consumer = &CharConsumer,
             .status_factory = [name_function](const Modifiers& modifiers) {
               return BuildStatus(name_function, modifiers);
             }};
         SetOptionsForBufferTransformation<Modifiers>(
             std::move(handler),
             [](const Modifiers& modifiers) {
               return modifiers.cursors_affected;
             },
             &options);
         return std::make_unique<CommandArgumentMode<Modifiers>>(
             std::move(options));
       }});
}

}  // namespace afc::editor
