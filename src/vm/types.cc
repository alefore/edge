#include "src/vm/types.h"

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::GetValueOrDie;
using afc::language::NewError;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;

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

language::ValueOrError<Identifier> IdentifierOrError(LazyString input) {
  if (input.IsEmpty()) return NewError(LazyString{L"Identifier is empty"});
  // TODO(trivial, 2023-12-30): Start checking characters (e.g., only alnum).
  return Identifier(input.ToString());
}

const Identifier& IdentifierAuto() {
  static const auto output = new Identifier(L"auto");
  return *output;
}

const Identifier& IdentifierInclude() {
  static const auto output = new Identifier(L"include");
  return *output;
}

PurityType CombinePurityType(const std::vector<PurityType>& types) {
  // We don't use `container::Fold` to enable early return.
  PurityType output = PurityType::kPure;
  for (PurityType a : types) switch (a) {
      case PurityType::kUnknown:
        return PurityType::kUnknown;
      case PurityType::kReader:
        output = PurityType::kReader;
        break;
      default:
        CHECK(output != PurityType::kUnknown);
    }
  return output;
}

namespace {
bool combine_purity_type_tests_registration =
    tests::Register(L"CombinePurityType", [] {
      auto t = [](PurityType a, PurityType b, PurityType expect) {
        std::stringstream value_stream;
        value_stream << a << " + " << b << " = " << expect;
        return tests::Test(
            {.name = FromByteString(value_stream.str()), .callback = [=] {
               CHECK(CombinePurityType({a, b}) == expect);
             }});
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

std::wstring TypesToString(const std::vector<Type>& types) {
  return Concatenate(types | std::views::transform([](const Type& t) {
                       // TODO(trivial, 2024-01-02): Change ToString to return a
                       // LazyString directly.
                       return LazyString{L"\""} + LazyString{ToString(t)} +
                              LazyString{L"\""};
                     }) |
                     Intersperse(LazyString{L", "}))
      .ToString();
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
                 return GetValueOrDie(function_purity_types,
                                      function_type.function_purity) +
                        L"<" + ToString(function_type.output.get()) + L"(" +
                        Concatenate(function_type.inputs |
                                    std::views::transform([](const Type& t) {
                                      // TODO(easy, 2024-01-02): Change ToString
                                      // to directly return a LazyString.
                                      return LazyString{ToString(t)};
                                    }) |
                                    Intersperse(LazyString{L", "}))
                            .ToString() +
                        L")>";
               }},
      type);
}

std::vector<NonNull<std::shared_ptr<gc::ObjectMetadata>>> ObjectType::Expand()
    const {
  return container::MaterializeVector(fields_ | std::views::values |
                                      gc::view::ObjectMetadata);
}

/* static */ gc::Root<ObjectType> ObjectType::New(gc::Pool& pool, Type type) {
  return pool.NewRoot(
      MakeNonNullUnique<ObjectType>(std::move(type), ConstructorAccessKey()));
}

ObjectType::ObjectType(const Type& type, ConstructorAccessKey) : type_(type) {}

void ObjectType::AddField(const Identifier& name, gc::Ptr<Value> field) {
  fields_.insert({name, std::move(field)});
}

std::vector<NonNull<Value*>> ObjectType::LookupField(
    const Identifier& name) const {
  auto range = fields_.equal_range(name);
  return container::MaterializeVector(
      std::ranges::subrange(range.first, range.second) |
      std::views::transform([](const auto& p) {
        return NonNull<Value*>::AddressOf(p.second.value());
      }));
}

void ObjectType::ForEachField(
    std::function<void(const Identifier&, Value&)> callback) {
  for (auto& it : fields_) callback(it.first, it.second.value());
}

void ObjectType::ForEachField(
    std::function<void(const Identifier&, const Value&)> callback) const {
  for (const auto& it : fields_) callback(it.first, it.second.value());
}

}  // namespace afc::vm
