#include "src/modifiers.h"

#include "src/wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp) {
  os << "[" << bp.buffer_name << ":" << bp.position << "]";
  return os;
}

ostream& operator<<(ostream& os, const Modifiers& m) {
  os << "[structure: " << m.structure->ToString() << "][direction: ";
  switch (m.direction) {
    case FORWARDS:
      os << "forwards";
      break;
    case BACKWARDS:
      os << "backwards";
      break;
  }
  os << "][default direction: ";
  switch (m.default_direction) {
    case FORWARDS:
      os << "forwards";
      break;
    case BACKWARDS:
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
  os << "][repetitions: " << m.repetitions << "]";
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

void Modifiers::Register(vm::Environment* environment) {
  auto modifiers_type = std::make_unique<vm::ObjectType>(L"Modifiers");

  environment->Define(L"Modifiers",
                      vm::NewCallback(std::make_shared<Modifiers>));

  modifiers_type->AddField(
      L"set_backwards", vm::NewCallback([](std::shared_ptr<Modifiers> output) {
        output->direction = BACKWARDS;
        return output;
      }));

  modifiers_type->AddField(
      L"set_line", vm::NewCallback([](std::shared_ptr<Modifiers> output) {
        output->structure = StructureLine();
        return output;
      }));

  modifiers_type->AddField(
      L"set_paste_buffer_behavior",
      vm::NewCallback([](std::shared_ptr<Modifiers> output,
                         bool paste_buffer_behavior) {
        output->paste_buffer_behavior =
            paste_buffer_behavior ? Modifiers::PasteBufferBehavior::kDeleteInto
                                  : Modifiers::PasteBufferBehavior::kDoNothing;
        return output;
      }));

  modifiers_type->AddField(
      L"set_repetitions",
      vm::NewCallback([](std::shared_ptr<Modifiers> output, int repetitions) {
        output->repetitions = repetitions;
        return output;
      }));

  modifiers_type->AddField(
      L"set_boundary_end_neighbor",
      vm::NewCallback([](std::shared_ptr<Modifiers> output) {
        output->boundary_end = LIMIT_NEIGHBOR;
        return output;
      }));

  environment->DefineType(L"Modifiers", std::move(modifiers_type));
}

std::wstring Modifiers::Serialize() const {
  std::wstring output = L"Modifiers()";
  if (direction == BACKWARDS) {
    output += L".set_backwards()";
  }
  // TODO: Handle other structures.
  if (structure == StructureLine()) {
    output += L".set_line()";
  }
  if (repetitions != 1) {
    output += L".set_repetitions(" + std::to_wstring(repetitions) + L")";
  }
  if (boundary_end == LIMIT_NEIGHBOR) {
    output += L".set_boundary_end_neigbhr()";
  }
  return output;
}

}  // namespace editor
namespace vm {
/* static */
std::shared_ptr<editor::Modifiers>
VMTypeMapper<std::shared_ptr<editor::Modifiers>>::get(Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"Modifiers");
  CHECK(value->user_value != nullptr);
  return std::static_pointer_cast<editor::Modifiers>(value->user_value);
}

/* static */
Value::Ptr VMTypeMapper<std::shared_ptr<editor::Modifiers>>::New(
    std::shared_ptr<editor::Modifiers> value) {
  return Value::NewObject(L"Modifiers",
                          std::shared_ptr<void>(value, value.get()));
}

const VMType VMTypeMapper<std::shared_ptr<editor::Modifiers>>::vmtype =
    VMType::ObjectType(L"Modifiers");
}  // namespace vm
}  // namespace afc
