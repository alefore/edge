#include "../public/types.h"

#include <glog/logging.h>

#include "../public/value.h"
#include "src/language/hash.h"
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
struct hash<afc::vm::types::Number> {
  size_t operator()(const afc::vm::types::Number&) const { return 0; }
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
struct hash<afc::vm::types::Function> {
  size_t operator()(const afc::vm::types::Function& object) const {
    return afc::language::compute_hash(
        object.function_purity, object.output.get(),
        afc::language::MakeHashableIteratorRange(object.inputs));
  }
};

size_t hash<afc::vm::Type>::operator()(const afc::vm::Type& x) const {
  return std::visit(
      [](const auto& t) {
        return hash<typename std::remove_const<
            typename std::remove_reference<decltype(t)>::type>::type>()(t);
      },
      x);
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

types::ObjectName NameForType(Type variant_type) {
  return std::visit(
      overload{
          [](const types::Void&) { return types::ObjectName(L"void"); },
          [](const types::Bool&) { return types::ObjectName(L"bool"); },
          [](const types::Number&) { return types::ObjectName(L"number"); },
          [](const types::String&) { return types::ObjectName(L"string"); },
          [](const types::Symbol&) { return types::ObjectName(L"symbol"); },
          [](const types::ObjectName& object) { return object; },
          [](const types::Function&) {
            return types::ObjectName(L"function");
          }},
      variant_type);
}

namespace types {
bool operator==(const Void&, const Void&) { return true; }
bool operator==(const Bool&, const Bool&) { return true; }
bool operator==(const Number&, const Number&) { return true; }
bool operator==(const String&, const String&) { return true; }
bool operator==(const Symbol&, const Symbol&) { return true; }
bool operator==(const Function& a, const Function& b) {
  return a.output == b.output && a.inputs == b.inputs &&
         a.function_purity == b.function_purity;
}
}  // namespace types

std::ostream& operator<<(std::ostream& os, const Type& type) {
  using ::operator<<;
  os << ToString(type);
  return os;
}

wstring TypesToString(const std::vector<Type>& types) {
  wstring output;
  wstring separator = L"";
  for (auto& t : types) {
    output += separator + L"\"" + ToString(t) + L"\"";
    separator = L", ";
  }
  return output;
}

std::wstring TypesToString(const std::unordered_set<Type>& types) {
  return TypesToString(std::vector<Type>(types.cbegin(), types.cend()));
}

std::wstring ToString(const Type& type) {
  return std::visit(
      overload{[](const types::Void&) -> std::wstring { return L"void"; },
               [](const types::Bool&) -> std::wstring { return L"bool"; },
               [](const types::Number&) -> std::wstring { return L"number"; },
               [](const types::String&) -> std::wstring { return L"string"; },
               [](const types::Symbol&) -> std::wstring { return L"symbol"; },
               [](const types::ObjectName& object) -> std::wstring {
                 return object.read();
               },
               [](const types::Function& function_type) -> std::wstring {
                 const std::unordered_map<PurityType, std::wstring>
                     function_purity_types = {
                         {PurityType::kPure, L"function"},
                         {PurityType::kReader, L"Function"},
                         {PurityType::kUnknown, L"FUNCTION"}};
                 wstring output =
                     function_purity_types.find(function_type.function_purity)
                         ->second +
                     L"<" + ToString(function_type.output.get()) + L"(";
                 wstring separator = L"";
                 for (const Type& input : function_type.inputs) {
                   output += separator + ToString(input);
                   separator = L", ";
                 }
                 output += L")>";
                 return output;
               }},
      type);
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> ObjectType::Expand()
    const {
  std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> output;
  for (auto& p : fields_) output.push_back(p.second.object_metadata());
  return output;
}

/* static */ gc::Root<ObjectType> ObjectType::New(gc::Pool& pool, Type type) {
  return pool.NewRoot(
      MakeNonNullUnique<ObjectType>(std::move(type), ConstructorAccessKey()));
}

ObjectType::ObjectType(const Type& type, ConstructorAccessKey) : type_(type) {}

void ObjectType::AddField(const wstring& name, gc::Ptr<Value> field) {
  fields_.insert({name, std::move(field)});
}

std::vector<NonNull<Value*>> ObjectType::LookupField(
    const wstring& name) const {
  std::vector<NonNull<Value*>> output;
  auto range = fields_.equal_range(name);

  for (auto it = range.first; it != range.second; ++it) {
    output.push_back(NonNull<Value*>::AddressOf(it->second.value()));
  }
  return output;
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
