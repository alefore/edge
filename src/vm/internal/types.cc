#include "../public/types.h"

#include <glog/logging.h>

#include "../public/value.h"

namespace std {
size_t hash<afc::vm::VMType>::operator()(const afc::vm::VMType& x) const {
  size_t output = hash<size_t>()(static_cast<size_t>(x.type)) ^
                  hash<wstring>()(x.object_type);
  for (const auto& a : x.type_arguments) {
    output ^= hash()(a);
  }
  return output;
}
}  // namespace std
namespace afc::vm {
using language::NonNull;

namespace gc = language::gc;

bool operator==(const VMType& lhs, const VMType& rhs) {
  return lhs.type == rhs.type && lhs.type_arguments == rhs.type_arguments &&
         lhs.function_purity == rhs.function_purity &&
         lhs.object_type == rhs.object_type;
}

std::ostream& operator<<(std::ostream& os, const VMType& type) {
  using ::operator<<;
  os << type.ToString();
  return os;
}

/* static */ const VMType& VMType::Void() {
  static VMType type(VMType::Type::kVoid);
  return type;
}

/* static */ const VMType& VMType::Bool() {
  static VMType type(VMType::Type::kBool);
  return type;
}

/* static */ const VMType& VMType::Integer() {
  static VMType type(VMType::Type::kInt);
  return type;
}

/* static */ const VMType& VMType::String() {
  static VMType type(VMType::Type::kString);
  return type;
}

/* static */ const VMType& VMType::Symbol() {
  static VMType type(VMType::Type::kSymbol);
  return type;
}

/* static */ const VMType& VMType::Double() {
  static VMType type(VMType::Type::kDouble);
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

/* static */ VMType VMType::ObjectType(const wstring& name) {
  VMType output(VMType::Type::kObject);
  output.object_type = name;
  return output;
}

wstring VMType::ToString() const {
  switch (type) {
    case Type::kVoid:
      return L"void";
    case Type::kBool:
      return L"bool";
    case Type::kInt:
      return L"int";
    case Type::kString:
      return L"string";
    case Type::kSymbol:
      return L"symbol";
    case Type::kDouble:
      return L"double";
    case Type::kFunction: {
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
    case Type::kObject:
      return object_type;
  }
  return L"unknown";
}

/* static */ VMType VMType::Function(vector<VMType> arguments,
                                     PurityType function_purity) {
  VMType output(VMType::Type::kFunction);
  output.type_arguments = arguments;
  output.function_purity = function_purity;
  return output;
}

ObjectType::ObjectType(const VMType& type) : type_(type) {}

ObjectType::ObjectType(const wstring& type_name)
    : ObjectType(VMType::ObjectType(type_name)) {}

void ObjectType::AddField(const wstring& name, gc::Root<Value> field) {
  fields_.insert({name, std::move(field)});
}

void ObjectType::ForEachField(
    std::function<void(const wstring&, Value&)> callback) {
  for (auto& it : fields_) callback(it.first, it.second.ptr().value());
}

}  // namespace afc::vm
