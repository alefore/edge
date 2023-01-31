#include "../public/types.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace std {
template <>
struct hash<afc::vm::types::Void> {
  size_t operator()(const afc::vm::types::Void&) const { return 0; }
};

template <>
struct hash<afc::vm::types::Bool> {
  size_t operator()(const afc::vm::types::Bool&) const { return 0; }
};

template <>
struct hash<afc::vm::types::Int> {
  size_t operator()(const afc::vm::types::Int&) const { return 0; }
};

template <>
struct hash<afc::vm::types::String> {
  size_t operator()(const afc::vm::types::String&) const { return 0; }
};

template <>
struct hash<afc::vm::types::Symbol> {
  size_t operator()(const afc::vm::types::Symbol&) const { return 0; }
};

template <>
struct hash<afc::vm::types::Double> {
  size_t operator()(const afc::vm::types::Double&) const { return 0; }
};

template <>
struct hash<afc::vm::types::Object> {
  size_t operator()(const afc::vm::types::Object& object) const {
    return std::hash<afc::vm::VMTypeObjectTypeName>()(object.object_type_name);
  }
};

size_t hash<afc::vm::VMType>::operator()(const afc::vm::VMType& x) const {
  size_t output =
      hash<size_t>()(static_cast<size_t>(x.type)) ^
      std::visit(
          [](const auto& t) {
            return hash<typename std::remove_const<
                typename std::remove_reference<decltype(t)>::type>::type>()(t);
          },
          x.variant);
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
      overload{
          [](const types::Void&) { return VMTypeObjectTypeName(L"void"); },
          [](const types::Bool&) { return VMTypeObjectTypeName(L"bool"); },
          [](const types::Int&) { return VMTypeObjectTypeName(L"int"); },
          [](const types::String&) { return VMTypeObjectTypeName(L"string"); },
          [](const types::Symbol&) { return VMTypeObjectTypeName(L"symbol"); },
          [](const types::Double&) { return VMTypeObjectTypeName(L"double"); },
          [](const types::Object& object) { return object.object_type_name; },
      },
      variant_type);
}

namespace types {
bool operator==(const Void&, const Void&) { return true; }
bool operator==(const Bool&, const Bool&) { return true; }
bool operator==(const Int&, const Int&) { return true; }
bool operator==(const String&, const String&) { return true; }
bool operator==(const Symbol&, const Symbol&) { return true; }
bool operator==(const Double&, const Double&) { return true; }
bool operator==(const Object& a, const Object& b) {
  return a.object_type_name == b.object_type_name;
}
}  // namespace types

bool operator==(const VMType& lhs, const VMType& rhs) {
  if (lhs.type != rhs.type) return false;
  switch (lhs.type) {
    case VMType::Type::kFunction:
      return lhs.type_arguments == rhs.type_arguments &&
             lhs.function_purity == rhs.function_purity;
    case VMType::Type::kVariant:
      return lhs.variant == rhs.variant;
  }
  LOG(FATAL) << "Invalid type";
  return false;
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
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Int() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::Int();
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::String() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::String();
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Symbol() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::Symbol();
    return output;
  }();
  return type;
}

/* static */ const VMType& VMType::Double() {
  static const VMType type = [] {
    VMType output(VMType::Type::kVariant);
    output.variant = types::Double();
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
  VMType output(VMType::Type::kVariant);
  output.variant = types::Object{.object_type_name = std::move(name)};
  return output;
}

wstring VMType::ToString() const {
  switch (type) {
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
    case Type::kVariant:
      return std::visit(
          overload{
              [](const types::Void&) -> std::wstring { return L"void"; },
              [](const types::Bool&) -> std::wstring { return L"bool"; },
              [](const types::Int&) -> std::wstring { return L"int"; },
              [](const types::String&) -> std::wstring { return L"string"; },
              [](const types::Symbol&) -> std::wstring { return L"symbol"; },
              [](const types::Double&) -> std::wstring { return L"double"; },
              [](const types::Object& object) -> std::wstring {
                return object.object_type_name.read();
              }},
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

/* static */ gc::Root<ObjectType> ObjectType::New(gc::Pool& pool, VMType type) {
  return pool.NewRoot(
      MakeNonNullUnique<ObjectType>(std::move(type), ConstructorAccessKey()));
}

/* static */ gc::Root<ObjectType> ObjectType::New(
    gc::Pool& pool, VMTypeObjectTypeName object_type_name) {
  return New(pool, VMType::ObjectType(object_type_name));
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
