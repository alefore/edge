#include "src/modifiers.h"

#include "src/language/lazy_string/lazy_string.h"
#include "src/language/wstring.h"

namespace gc = afc::language::gc;
using afc::language::MakeNonNullShared;
using afc::language::MakeNonNullUnique;
using afc::language::NonNull;
using afc::language::lazy_string::LazyString;
using afc::vm::Identifier;
using afc::vm::kPurityTypePure;
using afc::vm::kPurityTypeUnknown;

namespace afc::vm {
template <>
const types::ObjectName VMTypeMapper<
    NonNull<std::shared_ptr<editor::Modifiers>>>::object_type_name =
    types::ObjectName(L"Modifiers");
}  // namespace afc::vm
namespace afc::editor {

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp) {
  os << "[" << bp.buffer_name << ":" << bp.position << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Modifiers& m) {
  os << "[structure: " << m.structure << "][direction: ";
  switch (m.direction) {
    case Direction::kForwards:
      os << "forwards";
      break;
    case Direction::kBackwards:
      os << "backwards";
      break;
  }
  os << "][default direction: ";
  switch (m.default_direction) {
    case Direction::kForwards:
      os << "forwards";
      break;
    case Direction::kBackwards:
      os << "backwards";
      break;
  }
  os << "][paste_buffer_behavior: ";
  switch (m.paste_buffer_behavior) {
    case Modifiers::PasteBufferBehavior::kDeleteInto:
      os << "kDeleteInto";
      break;
    case Modifiers::PasteBufferBehavior::kDoNothing:
      os << "kDoNothing";
      break;
  }
  os << "]";
  if (m.repetitions.has_value()) {
    os << "[repetitions: " << m.repetitions.value() << "]";
  }
  return os;
}

Modifiers::Boundary IncrementBoundary(Modifiers::Boundary boundary) {
  switch (boundary) {
    case Modifiers::CURRENT_POSITION:
      return Modifiers::LIMIT_CURRENT;
    case Modifiers::LIMIT_CURRENT:
      return Modifiers::LIMIT_NEIGHBOR;
    case Modifiers::LIMIT_NEIGHBOR:
      return Modifiers::CURRENT_POSITION;
  }
  CHECK(false);
  return Modifiers::CURRENT_POSITION;  // Silence warning.
}

void Modifiers::Register(language::gc::Pool& pool,
                         vm::Environment& environment) {
  using vm::ObjectType;
  using vm::PurityType;

  gc::Root<ObjectType> modifiers_type = ObjectType::New(
      pool,
      vm::VMTypeMapper<NonNull<std::shared_ptr<Modifiers>>>::object_type_name);

  environment.Define(
      Identifier{LazyString{L"Modifiers"}},
      vm::NewCallback(pool, kPurityTypePure, MakeNonNullShared<Modifiers>));

  modifiers_type.ptr()->AddField(
      Identifier{LazyString{L"set_backwards"}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Modifiers>> output) {
                        output->direction = Direction::kBackwards;
                        return output;
                      })
          .ptr());

  modifiers_type.ptr()->AddField(
      Identifier{LazyString{L"set_line"}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Modifiers>> output) {
                        output->structure = Structure::kLine;
                        return output;
                      })
          .ptr());

  modifiers_type.ptr()->AddField(
      Identifier{LazyString{L"set_delete_behavior"}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Modifiers>> output, bool delete_behavior) {
            output->text_delete_behavior =
                delete_behavior ? Modifiers::TextDeleteBehavior::kDelete
                                : Modifiers::TextDeleteBehavior::kKeep;
            return output;
          })
          .ptr());

  modifiers_type.ptr()->AddField(
      Identifier{LazyString{L"set_paste_buffer_behavior"}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Modifiers>> output,
                         bool paste_buffer_behavior) {
                        output->paste_buffer_behavior =
                            paste_buffer_behavior
                                ? Modifiers::PasteBufferBehavior::kDeleteInto
                                : Modifiers::PasteBufferBehavior::kDoNothing;
                        return output;
                      })
          .ptr());

  modifiers_type.ptr()->AddField(
      Identifier{LazyString{L"set_repetitions"}},
      vm::NewCallback(
          pool, kPurityTypeUnknown,
          [](NonNull<std::shared_ptr<Modifiers>> output, int repetitions) {
            output->repetitions = repetitions;
            return output;
          })
          .ptr());

  modifiers_type.ptr()->AddField(
      Identifier{LazyString{L"set_boundary_end_neighbor"}},
      vm::NewCallback(pool, kPurityTypeUnknown,
                      [](NonNull<std::shared_ptr<Modifiers>> output) {
                        output->boundary_end = LIMIT_NEIGHBOR;
                        return output;
                      })
          .ptr());

  environment.DefineType(modifiers_type.ptr());
}

std::wstring Modifiers::Serialize() const {
  std::wstring output = L"Modifiers()";
  if (direction == Direction::kBackwards) {
    output += L".set_backwards()";
  }
  // TODO: Handle other structures.
  if (structure == Structure::kLine) {
    output += L".set_line()";
  }
  if (repetitions.has_value()) {
    output +=
        L".set_repetitions(" + std::to_wstring(repetitions.value()) + L")";
  }
  if (boundary_end == LIMIT_NEIGHBOR) {
    output += L".set_boundary_end_neigbhr()";
  }
  return output;
}
}  // namespace afc::editor
