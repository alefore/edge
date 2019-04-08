#include "modifiers.h"

#include "wstring.h"

namespace afc {
namespace editor {

std::ostream& operator<<(std::ostream& os, const BufferPosition& bp) {
  os << "[" << bp.buffer_name << ":" << bp.position << "]";
  return os;
}

wstring StructureToString(Structure structure) {
  switch (structure) {
    case CHAR:
      return L"char";
    case WORD:
      return L"word";
    case SYMBOL:
      return L"symbol";
    case LINE:
      return L"line";
    case MARK:
      return L"mark";
    case PAGE:
      return L"page";
    case SEARCH:
      return L"search";
    case TREE:
      return L"tree";
    case CURSOR:
      return L"cursor";
    case BUFFER:
      return L"buffer";
    case PARAGRAPH:
      return L"paragraph";
  }
  CHECK(false);
}

ostream& operator<<(ostream& os, const Modifiers& m) {
  os << "[structure: " << StructureToString(m.structure) << "][strength: ";
  switch (m.strength) {
    case Modifiers::VERY_WEAK:
      os << "very weak";
      break;
    case Modifiers::WEAK:
      os << "weak";
      break;
    case Modifiers::DEFAULT:
      os << "default";
      break;
    case Modifiers::STRONG:
      os << "strong";
      break;
    case Modifiers::VERY_STRONG:
      os << "very strong";
      break;
  }
  os << "][direction: ";
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

  environment->Define(L"Modifiers", vm::NewCallback(std::function<Modifiers*()>(
                                        []() { return new Modifiers(); })));

  modifiers_type->AddField(
      L"set_line", vm::NewCallback(std::function<void(Modifiers*)>(
                       [](Modifiers* output) { output->structure = LINE; })));

  modifiers_type->AddField(L"set_repetitions",
                           vm::NewCallback(std::function<void(Modifiers*, int)>(
                               [](Modifiers* output, int repetitions) {
                                 output->repetitions = repetitions;
                               })));

  modifiers_type->AddField(
      L"set_boundary_end_neighbor",
      vm::NewCallback(std::function<void(Modifiers*)>(
          [](Modifiers* output) { output->boundary_end = LIMIT_NEIGHBOR; })));

  environment->DefineType(L"Modifiers", std::move(modifiers_type));
}

}  // namespace editor
namespace vm {
/* static */
editor::Modifiers* VMTypeMapper<editor::Modifiers*>::get(Value* value) {
  CHECK(value != nullptr);
  CHECK(value->type.type == VMType::OBJECT_TYPE);
  CHECK(value->type.object_type == L"Modifiers");
  CHECK(value->user_value != nullptr);
  return static_cast<editor::Modifiers*>(value->user_value.get());
}

/* static */
Value::Ptr VMTypeMapper<editor::Modifiers*>::New(editor::Modifiers* value) {
  return Value::NewObject(
      L"Modifiers",
      shared_ptr<void>(new editor::Modifiers(*value), [](void* v) {
        delete static_cast<editor::Modifiers*>(v);
      }));
}

const VMType VMTypeMapper<editor::Modifiers*>::vmtype =
    VMType::ObjectType(L"Modifiers");
}  // namespace vm
}  // namespace afc
