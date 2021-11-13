#include "../public/types.h"

#include <glog/logging.h>

#include "../public/value.h"

namespace std {
size_t hash<afc::vm::VMType>::operator()(const afc::vm::VMType& x) const {
  size_t output = hash<int>()(x.type) ^ hash<wstring>()(x.object_type);
  for (const auto& a : x.type_arguments) {
    output ^= hash()(a);
  }
  return output;
}
}  // namespace std
namespace afc {
namespace vm {

bool operator==(const VMType& lhs, const VMType& rhs) {
  return lhs.type == rhs.type && lhs.type_arguments == rhs.type_arguments &&
         lhs.function_purity == rhs.function_purity &&
         lhs.object_type == rhs.object_type;
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

wstring TypesToString(const std::vector<VMType>& types) {
  wstring output;
  wstring separator = L"";
  for (auto& t : types) {
    output += separator + L"\"" + t.ToString() + L"\"";
    separator = L", ";
  }
  return output;
}

std::wstring TypesToString(const std::unordered_set<VMType>& types) {
  return TypesToString(std::vector<VMType>(types.cbegin(), types.cend()));
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
    case VM_VOID:
      return L"void";
    case VM_BOOLEAN:
      return L"bool";
    case VM_INTEGER:
      return L"int";
    case VM_STRING:
      return L"string";
    case VM_SYMBOL:
      return L"symbol";
    case VM_DOUBLE:
      return L"double";
    case ENVIRONMENT:
      return L"environment";
    case FUNCTION: {
      CHECK(!type_arguments.empty());
      wstring output =
          wstring(function_purity == PurityType::kPure ? L"function"
                                                       : L"Function") +
          L"<" + type_arguments[0].ToString() + L"(";
      wstring separator = L"";
      for (size_t i = 1; i < type_arguments.size(); i++) {
        output += separator + type_arguments[i].ToString();
        separator = L", ";
      }
      output += L")>";
      return output;
    }
    case OBJECT_TYPE:
      return object_type;
  }
  return L"unknown";
}

/* static */ VMType VMType::Function(vector<VMType> arguments,
                                     PurityType function_purity) {
  VMType output(VMType::FUNCTION);
  output.type_arguments = arguments;
  output.function_purity = function_purity;
  return output;
}

ObjectType::ObjectType(const VMType& type) : type_(type) {}

ObjectType::ObjectType(const wstring& type_name)
    : ObjectType(VMType::ObjectType(type_name)) {}

void ObjectType::AddField(const wstring& name, std::unique_ptr<Value> field) {
  fields_.insert({name, std::move(field)});
}

void ObjectType::ForEachField(
    std::function<void(const wstring&, Value*)> callback) {
  for (auto& it : fields_) {
    callback(it.first, it.second.get());
  }
}

}  // namespace vm
}  // namespace afc
