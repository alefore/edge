#include "src/command_with_modifiers.h"

#include <memory>

#include "src/buffer_variables.h"
#include "src/editor.h"
#include "src/set_mode_command.h"
#include "src/terminal.h"
#include "src/transformation_argument_mode.h"

namespace afc::editor {
namespace {
std::unordered_map<wint_t, TransformationArgumentMode<Modifiers>::CharHandler>
GetMap() {
  std::unordered_map<wint_t, TransformationArgumentMode<Modifiers>::CharHandler>
      output;
  output['+'] = {.apply = [](Modifiers modifiers) {
    modifiers.repetitions = modifiers.repetitions.value_or(1) + 1;
    return modifiers;
  }};

  output['-'] = {.apply = [](Modifiers modifiers) {
    if (modifiers.repetitions.value_or(1) > 0) {
      modifiers.repetitions = modifiers.repetitions.value_or(1) - 1;
    }
    return modifiers;
  }};

  output['*'] = {.apply = [](Modifiers modifiers) {
    switch (modifiers.cursors_affected.value_or(
        Modifiers::kDefaultCursorsAffected)) {
      case Modifiers::CursorsAffected::kOnlyCurrent:
        modifiers.cursors_affected = Modifiers::CursorsAffected::kAll;
        break;
      case Modifiers::CursorsAffected::kAll:
        modifiers.cursors_affected = Modifiers::CursorsAffected::kOnlyCurrent;
        break;
    }
    return modifiers;
  }};

  for (int i = 0; i < 10; i++) {
    output['0' + i] = {.apply = [i](Modifiers modifiers) {
      modifiers.repetitions = 10 * modifiers.repetitions.value_or(1) + i;
      return modifiers;
    }};
  }

  output['('] = {.apply = [](Modifiers modifiers) {
    modifiers.boundary_begin = Modifiers::CURRENT_POSITION;
    return modifiers;
  }};

  output['['] = {.apply = [](Modifiers modifiers) {
    modifiers.boundary_begin = Modifiers::LIMIT_CURRENT;
    return modifiers;
  }};

  output['{'] = {.apply = [](Modifiers modifiers) {
    modifiers.boundary_begin = Modifiers::LIMIT_NEIGHBOR;
    return modifiers;
  }};

  output[')'] = {.apply = [](Modifiers modifiers) {
    modifiers.boundary_end = Modifiers::CURRENT_POSITION;
    return modifiers;
  }};

  output[']'] = {.apply = [](Modifiers modifiers) {
    if (modifiers.boundary_end == Modifiers::CURRENT_POSITION) {
      modifiers.boundary_end = Modifiers::LIMIT_CURRENT;
    } else if (modifiers.boundary_end == Modifiers::LIMIT_CURRENT) {
      modifiers.boundary_end = Modifiers::LIMIT_NEIGHBOR;
    } else if (modifiers.boundary_end == Modifiers::LIMIT_NEIGHBOR) {
      modifiers.boundary_end = Modifiers::LIMIT_CURRENT;
      if (!modifiers.repetitions.has_value()) {
        modifiers.repetitions = 1;
      }
      ++modifiers.repetitions.value();
    }
    return modifiers;
  }};

  output['r'] = {.apply = [](Modifiers modifiers) {
    modifiers.direction = ReverseDirection(modifiers.direction);
    return modifiers;
  }};

  auto set_structure = [&output](wint_t c, Structure* structure) {
    output[c] = {.apply = [structure](Modifiers modifiers) {
      modifiers.structure =
          modifiers.structure == structure ? StructureChar() : structure;
      return modifiers;
    }};
  };

  set_structure('e', StructureLine());
  set_structure('w', StructureWord());
  set_structure('W', StructureSymbol());
  set_structure('B', StructureBuffer());
  set_structure('c', StructureCursor());
  set_structure('t', StructureTree());
  set_structure('S', StructureSentence());
  set_structure('p', StructureParagraph());

  output['P'] = {.apply = [](Modifiers modifiers) {
    modifiers.paste_buffer_behavior =
        modifiers.paste_buffer_behavior ==
                Modifiers::PasteBufferBehavior::kDeleteInto
            ? Modifiers::PasteBufferBehavior::kDoNothing
            : Modifiers::PasteBufferBehavior::kDeleteInto;
    return modifiers;
  }};

  output['k'] = {.apply = [](Modifiers modifiers) {
    modifiers.delete_behavior =
        modifiers.delete_behavior == Modifiers::DeleteBehavior::kDeleteText
            ? Modifiers::DeleteBehavior::kDoNothing
            : Modifiers::DeleteBehavior::kDeleteText;
    return modifiers;
  }};
  return output;
}

std::wstring BuildStatus(std::wstring name, const Modifiers& modifiers) {
  std::wstring status = name;
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
    wstring name, wstring description, Modifiers modifiers,
    CommandWithModifiersHandler handler, EditorState* editor_state) {
  return NewSetModeCommand(
      {.description = description,
       .category = L"Edit",
       .factory = [editor_state, name, modifiers,
                   handler = std::move(handler)] {
         const auto characters_map = std::make_shared<std::unordered_map<
             wint_t, TransformationArgumentMode<Modifiers>::CharHandler>>(
             GetMap());
         Modifiers mutable_modifiers = modifiers;
         // TODO: Find a way to have this honor `multiple_cursors`. Perhaps the
         // best way is to get rid of that? Or somehow merge that with
         // Modifiers.cursors_affected.
         if (editor_state->modifiers().cursors_affected.has_value()) {
           mutable_modifiers.cursors_affected =
               editor_state->modifiers().cursors_affected;
         }
         return std::make_unique<TransformationArgumentMode<Modifiers>>(
             TransformationArgumentMode<Modifiers>::Options{
                 .editor_state = editor_state,
                 .initial_value = std::move(mutable_modifiers),
                 .transformation_factory = handler,
                 .characters = characters_map,
                 .status_factory =
                     [name](const Modifiers& modifiers) {
                       return BuildStatus(name, modifiers);
                     },
                 .cursors_affected_factory =
                     [editor_state](const Modifiers& modifiers) {
                       return modifiers.cursors_affected;
                     }});
       }});
}

}  // namespace afc::editor
