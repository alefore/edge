#include "../public/types.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "src/language/overload.h"
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
using language::MakeNonNullUnique;
using language::NonNull;
using language::overload;

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

VMTypeObjectTypeName NameForType(Type variant_type) {
  return std::visit(
      overload{[](types::Void) { return VMTypeObjectTypeName(L"void"); },
               [](types::Bool) { return VMTypeObjectTypeName(L"bool"); },
               [](types::Int) { return VMTypeObjectTypeName(L"int"); },
               [](types::String) { return VMTypeObjectTypeName(L"string"); }},
      variant_type);
}

namespace types {
bool operator==(const Void&, const Void&) { return true; }
bool operator==(const Bool&, const Bool&) { return true; }
}  // namespace types

bool operator==(const VMType& lhs, const VMType& rhs) {
  return types::Void() == types::Void();
  return lhs.type == rhs.type && lhs.type_arguments == rhs.type_arguments &&
         lhs.function_purity == rhs.function_purity &&
         lhs.object_type == rhs.object_type && (lhs.variant == rhs.variant);
}

std::ostream& operator<<(std::ostream& os, const VMType& type) {
  using ::operator<<;
  os << type.ToString();
  return os;
}

/* static */ const VMType& VMType::Void() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::Void();
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Bool() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::Bool();
    output.object_type = VMTypeObjectTypeName(L"bool");
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Int() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::Int();
    output.object_type = VMTypeObjectTypeName(L"int");
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::String() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::String();
    output.object_type = VMTypeObjectTypeName(L"string");
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Symbol() {
  static const VMType type(VMType::Type::kSymbol);
  return type;
}

/* static */ const VMType& VMType::Double() {
  static const VMType type = [] {
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
    case Type::kVariant:
      return std::visit(
          overload{[](const types::Void&) { return L"void"; },
                   [](const types::Bool&) { return L"bool"; },
                   [](const types::Int&) { return L"int"; },
                   [](const types::String&) { return L"string"; }},
          variant);
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

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> ObjectType::Expand()
    const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  for (auto& p : fields_) output.push_back(p.second.object_metadata());
  return output;
}

/* static */ language::gc::Root<ObjectType> ObjectType::New(gc::Pool& pool,
                                                            VMType type) {
  return pool.NewRoot(
      MakeNonNullUnique<ObjectType>(std::move(type), ConstructorAccessKey()));
}

ObjectType::ObjectType(const VMType& type, ConstructorAccessKey)
    : type_(type) {}

void ObjectType::AddField(const wstring& name, gc::Ptr<Value> field) {
  fields_.insert({name, std::move(field)});
}

Value* ObjectType::LookupField(const wstring& name) const {
  auto it = fields_.find(name);
  return it == fields_.end() ? nullptr : &it->second.value();
}

void ObjectType::ForEachField(
    std::function<void(const wstring&, Value&)> callback) {
  for (auto& it : fields_) callback(it.first, it.second.value());
}

void ObjectType::ForEachField(
    std::function<void(const wstring&, const Value&)> callback) const {
  for (const auto& it : fields_) callback(it.first, it.second.value());
}

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<NonNull<std::shared_ptr<ObjectMetadata>>> Expand(
    const afc::vm::ObjectType& t) {
  return t.Expand();
}
}  // namespace afc::language::gc
