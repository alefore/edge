#include "../public/types.h"
#include "../public/value.h"

namespace afc {
namespace vm {

bool operator==(const VMType& lhs, const VMType& rhs) {
  return lhs.type == rhs.type && lhs.type_arguments == rhs.type_arguments;
}

/* static */ const VMType& VMType::Void() {
  static VMType type(VMType::VM_VOID);
  return type;
}

/* static */ const VMType& VMType::Bool() {
  static VMType type(VMType::VM_BOOLEAN);
  return type;
}

/* static */ const VMType& VMType::Integer() {
  static VMType type(VMType::VM_INTEGER);
  return type;
}

/* static */ const VMType& VMType::String() {
  static VMType type(VMType::VM_STRING);
  return type;
}

/* static */ VMType VMType::ObjectType(afc::vm::ObjectType* type) {
  return ObjectType(type->type().object_type);
}

/* static */ VMType VMType::ObjectType(const string& name) {
  VMType output(VMType::OBJECT_TYPE);
  output.object_type = name;
  return output;
}

string VMType::ToString() const {
  switch (type) {
    case VM_VOID: return "void";
    case VM_BOOLEAN: return "bool";
    case VM_INTEGER: return "int";
    case VM_STRING: return "string";
    case VM_SYMBOL: return "symbol";
    case ENVIRONMENT: return "environment";
    case FUNCTION: return "function";
    case OBJECT_TYPE: return object_type;
  }
  return "unknown";
}

ObjectType::ObjectType(const VMType& type)
    : type_(type),
      fields_(new map<string, unique_ptr<Value>>) {}

ObjectType::ObjectType(const string& type_name)
    : type_(VMType::ObjectType(type_name)),
      fields_(new map<string, unique_ptr<Value>>) {}

void ObjectType::AddField(const string& name, unique_ptr<Value> field) {
  auto it = fields_->insert(make_pair(name, nullptr));
  it.first->second = std::move(field);
}

}  // namespace vm
}  // namespace afc
