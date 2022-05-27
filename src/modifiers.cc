#include "src/modifiers.h"

#include "src/language/wstring.h"

namespace afc::editor {
using language::MakeNonNullUnique;
namespace gc = language::gc;

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp) {
  os << "[" << bp.buffer_name << ":" << bp.position << "]";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Modifiers& m) {
  os << "[structure: " << m.structure->ToString() << "][direction: ";
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
  using vm::PurityType;

  auto modifiers_type = MakeNonNullUnique<vm::ObjectType>(
      vm::VMTypeMapper<std::shared_ptr<Modifiers>>::vmtype);

  environment.Define(L"Modifiers",
                     vm::NewCallback(pool, PurityType::kUnknown, []() {
                       return std::make_shared<Modifiers>();
                     }));

  modifiers_type->AddField(
      L"set_backwards", vm::NewCallback(pool, PurityType::kUnknown,
                                        [](std::shared_ptr<Modifiers> output) {
                                          output->direction =
                                              Direction::kBackwards;
                                          return output;
                                        }));

  modifiers_type->AddField(
      L"set_line", vm::NewCallback(pool, PurityType::kUnknown,
                                   [](std::shared_ptr<Modifiers> output) {
                                     output->structure = StructureLine();
                                     return output;
                                   }));

  modifiers_type->AddField(
      L"set_delete_behavior",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](std::shared_ptr<Modifiers> output, bool delete_behavior) {
            output->text_delete_behavior =
                delete_behavior ? Modifiers::TextDeleteBehavior::kDelete
                                : Modifiers::TextDeleteBehavior::kKeep;
            return output;
          }));

  modifiers_type->AddField(
      L"set_paste_buffer_behavior",
      vm::NewCallback(
          pool, PurityType::kUnknown,
          [](std::shared_ptr<Modifiers> output, bool paste_buffer_behavior) {
            output->paste_buffer_behavior =
                paste_buffer_behavior
                    ? Modifiers::PasteBufferBehavior::kDeleteInto
                    : Modifiers::PasteBufferBehavior::kDoNothing;
            return output;
          }));

  modifiers_type->AddField(
      L"set_repetitions",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](std::shared_ptr<Modifiers> output, int repetitions) {
                        output->repetitions = repetitions;
                        return output;
                      }));

  modifiers_type->AddField(
      L"set_boundary_end_neighbor",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](std::shared_ptr<Modifiers> output) {
                        output->boundary_end = LIMIT_NEIGHBOR;
                        return output;
                      }));

  environment.DefineType(std::move(modifiers_type));
}

std::wstring Modifiers::Serialize() const {
  std::wstring output = L"Modifiers()";
  if (direction == Direction::kBackwards) {
    output += L".set_backwards()";
  }
  // TODO: Handle other structures.
  if (structure == StructureLine()) {
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
namespace afc::vm {
using language::NonNull;
namespace gc = language::gc;

/* static */
std::shared_ptr<editor::Modifiers>
VMTypeMapper<std::shared_ptr<editor::Modifiers>>::get(Value& value) {
  // TODO(easy, 2022-05-27): Return the NonNull?
  return NonNull<std::shared_ptr<editor::Modifiers>>::StaticCast(
             value.get_user_value(vmtype))
      .get_shared();
}

/* static */
gc::Root<Value> VMTypeMapper<std::shared_ptr<editor::Modifiers>>::New(
    language::gc::Pool& pool, std::shared_ptr<editor::Modifiers> value) {
  // TODO(easy, 2022-05-27): Receive the parameter as NonNull.
  NonNull<std::shared_ptr<editor::Modifiers>> value_void =
      NonNull<std::shared_ptr<editor::Modifiers>>::Unsafe(value);
  return Value::NewObject(
      pool,
      VMTypeMapper<std::shared_ptr<editor::Modifiers>>::vmtype.object_type,
      value_void);
}

const VMType VMTypeMapper<std::shared_ptr<editor::Modifiers>>::vmtype =
    VMType::ObjectType(VMTypeObjectTypeName(L"Modifiers"));
}  // namespace afc::vm
