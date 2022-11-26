#include "../public/types.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace std {
size_t hash<afc::vm::VMType>::operator()(const afc::vm::VMType& x) const {
  size_t output = hash<size_t>()(static_cast<size_t>(x.type)) ^
                  hash<afc::vm::VMTypeObjectTypeName>()(x.object_type);
  for (const auto& a : x.type_arguments) {
    output ^= hash()(a);
  }
  return output;
}
}  // namespace std
namespace afc::vm {
using language::FromByteString;
using language::NonNull;

namespace gc = language::gc;

PurityType CombinePurityType(PurityType a, PurityType b) {
  if (a == PurityType::kUnknown || b == PurityType::kUnknown)
    return PurityType::kUnknown;
  if (a == PurityType::kReader || b == PurityType::kReader)
    return PurityType::kReader;
  CHECK(a == PurityType::kPure);
  CHECK(b == PurityType::kPure);
  return PurityType::kPure;
}

namespace {
bool combine_purity_type_tests_registration =
    tests::Register(L"CombinePurityType", [] {
      auto t = [](PurityType a, PurityType b, PurityType expect) {
        std::stringstream value_stream;
        value_stream << a << " + " << b << " = " << expect;
        return tests::Test(
            {.name = FromByteString(value_stream.str()),
             .callback = [=] { CHECK(CombinePurityType(a, b) == expect); }});
      };
      return std::vector<tests::Test>(
          {t(PurityType::kPure, PurityType::kPure, PurityType::kPure),
           t(PurityType::kPure, PurityType::kReader, PurityType::kReader),
           t(PurityType::kPure, PurityType::kUnknown, PurityType::kUnknown),
           t(PurityType::kReader, PurityType::kPure, PurityType::kReader),
           t(PurityType::kReader, PurityType::kReader, PurityType::kReader),
           t(PurityType::kReader, PurityType::kUnknown, PurityType::kUnknown),
           t(PurityType::kUnknown, PurityType::kPure, PurityType::kUnknown),
           t(PurityType::kUnknown, PurityType::kReader, PurityType::kUnknown),
           t(PurityType::kUnknown, PurityType::kUnknown,
             PurityType::kUnknown)});
    }());
}

std::ostream& operator<<(std::ostream& os, const PurityType& value) {
  switch (value) {
    case PurityType::kPure:
      os << "pure";
      break;
    case PurityType::kReader:
      os << "reader";
      break;
    case PurityType::kUnknown:
      os << "unknown";
      break;
  }
  return os;
}

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
  static VMType type = [] {
    VMType output(VMType::Type::kBool);
    output.object_type = VMTypeObjectTypeName(L"bool");
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Int() {
  static VMType type = [] {
    VMType output(VMType::Type::kInt);
    output.object_type = VMTypeObjectTypeName(L"int");
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::String() {
  static VMType type = [] {
    VMType output(VMType::Type::kString);
    output.object_type = VMTypeObjectTypeName(L"string");
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Symbol() {
  static VMType type(VMType::Type::kSymbol);
  return type;
}

/* static */ const VMType& VMType::Double() {
  static VMType type = [] {
    VMType output(VMType::Type::kDouble);
    output.object_type = VMTypeObjectTypeName(L"double");
    return output;
  }();
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

/* static */ VMType VMType::ObjectType(VMTypeObjectTypeName name) {
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
      const std::unordered_map<PurityType, std::wstring> function_purity_types =
          {{PurityType::kPure, L"function"},
           {PurityType::kReader, L"Function"},
           {PurityType::kUnknown, L"FUNCTION"}};
      wstring output = function_purity_types.find(function_purity)->second +
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
      return object_type.read();
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

ObjectType::ObjectType(VMTypeObjectTypeName type_name)
    : ObjectType(VMType::ObjectType(type_name)) {}

void ObjectType::AddField(const wstring& name, gc::Root<Value> field) {
  fields_.insert({name, std::move(field)});
}

void ObjectType::ForEachField(
    std::function<void(const wstring&, Value&)> callback) {
  for (auto& it : fields_) callback(it.first, it.second.ptr().value());
}

void ObjectType::ForEachField(
    std::function<void(const wstring&, const Value&)> callback) const {
  for (const auto& it : fields_) callback(it.first, it.second.ptr().value());
}

}  // namespace afc::vm
