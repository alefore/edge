#include "../public/types.h"

#include <glog/logging.h>

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

/* static */ const VMType& VMType::Double() {
  static VMType type(VMType::VM_DOUBLE);
  return type;
}

/* static */ VMType VMType::ObjectType(afc::vm::ObjectType* type) {
  return ObjectType(type->type().object_type);
}

/* static */ VMType VMType::ObjectType(const wstring& name) {
  VMType output(VMType::OBJECT_TYPE);
  output.object_type = name;
  return output;
}

wstring VMType::ToString() const {
  switch (type) {
    case VM_VOID: return L"void";
    case VM_BOOLEAN: return L"bool";
    case VM_INTEGER: return L"int";
    case VM_STRING: return L"string";
    case VM_SYMBOL: return L"symbol";
    case VM_DOUBLE: return L"double";
    case ENVIRONMENT: return L"environment";
    case FUNCTION:
      {
        CHECK(!type_arguments.empty());
        wstring output = L"function<" + type_arguments[0].ToString() + L"(";
        wstring separator = L"";
        for (size_t i = 1; i < type_arguments.size(); i++) {
          output += separator + type_arguments[i].ToString();
          separator = L", ";
        }
        output += L")>";
        return output;
      }
    case OBJECT_TYPE: return object_type;
  }
  return L"unknown";
}

ObjectType::ObjectType(const VMType& type)
    : type_(type),
      fields_(new map<wstring, unique_ptr<Value>>) {}

ObjectType::ObjectType(const wstring& type_name)
    : type_(VMType::ObjectType(type_name)),
      fields_(new map<wstring, unique_ptr<Value>>) {}

void ObjectType::AddField(const wstring& name, unique_ptr<Value> field) {
  auto it = fields_->insert(make_pair(name, nullptr));
  it.first->second = std::move(field);
}

}  // namespace vm
}  // namespace afc
