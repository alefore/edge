#include "src/vm/types.h"

#include <glog/logging.h>

#include "src/language/container.h"
#include "src/language/gc_view.h"
#include "src/language/hash.h"
#include "src/language/lazy_string/append.h"
#include "src/language/lazy_string/char_buffer.h"
#include "src/language/lazy_string/functional.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/overload.h"
#include "src/language/wstring.h"
#include "src/tests/tests.h"

namespace container = afc::language::container;

using afc::language::Error;
using afc::language::GetValueOrDie;
using afc::language::PossibleError;
using afc::language::lazy_string::ColumnNumber;
using afc::language::lazy_string::ColumnNumberDelta;
using afc::language::lazy_string::Concatenate;
using afc::language::lazy_string::Intersperse;
using afc::language::lazy_string::LazyString;

namespace std {
size_t hash<afc::vm::PurityType>::operator()(
    const afc::vm::PurityType& x) const {
  return afc::language::compute_hash(x.writes_external_outputs,
                                     x.writes_local_variables,
                                     x.reads_external_inputs);
}

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

/* static */ PossibleError IdentifierValidator::Validate(
    const LazyString& input) {
  if (input.IsEmpty()) return Error{LazyString{L"Identifier can't be empty."}};
  // TODO(2024-08-27): Improve the validation? The presence of '~' is
  // questionable. Maybe we should validate that it only occurs in the
  // beginning? We should probably also validate that numbers don't occur in the
  // beginning.
  static const std::wstring allow_list_str =
      L"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      L"abcdefghijklmnopqrstuvwxyz"
      L"0123456789"
      L"_~";
  static const std::unordered_set<wchar_t> allow_list(allow_list_str.begin(),
                                                      allow_list_str.end());
  if (std::optional<ColumnNumber> position = FindFirstNotOf(input, allow_list);
      position.has_value())
    return Error{LazyString{L"Invalid character found inside identifier: "} +
                 input.Substring(position.value(), ColumnNumberDelta{1})};
  return language::Success();
}

const Identifier& IdentifierAuto() {
  static const auto output = new Identifier{LazyString{L"auto"}};
  return *output;
}

const Identifier& IdentifierInclude() {
  static const auto output = new Identifier{LazyString{L"include"}};
  return *output;
}

PurityType CombinePurityType(const std::vector<PurityType>& types) {
  return container::Fold(
      [](PurityType a, PurityType b) {
        return PurityType{
            .writes_external_outputs =
                a.writes_external_outputs || b.writes_external_outputs,
            .writes_local_variables =
                a.writes_local_variables || b.writes_local_variables,
            .reads_external_inputs =
                a.reads_external_inputs || b.reads_external_inputs};
      },
      PurityType{}, types);
}

namespace {
bool combine_purity_type_tests_registration =
    tests::Register(L"CombinePurityType", [] {
      auto t = [](PurityType a, PurityType b, PurityType expect) {
        std::stringstream value_stream;
        value_stream << a << " + " << b << " = " << expect;
        return tests::Test(
            {.name = FromByteString(value_stream.str()), .callback = [=] {
               CHECK_EQ(CombinePurityType({a, b}), expect);
               CHECK_EQ(CombinePurityType({b, a}), expect);
             }});
      };
      return std::vector<tests::Test>({
          t(kPurityTypePure, kPurityTypePure, kPurityTypePure),
          t(kPurityTypePure, kPurityTypeReader, kPurityTypeReader),
          t(kPurityTypePure, kPurityTypeUnknown, kPurityTypeUnknown),
          t(kPurityTypeReader, kPurityTypePure, kPurityTypeReader),
          t(kPurityTypeReader, kPurityTypeReader, kPurityTypeReader),
          t(kPurityTypeReader, kPurityTypeUnknown, kPurityTypeUnknown),
          t(kPurityTypeUnknown, kPurityTypePure, kPurityTypeUnknown),
          t(kPurityTypeUnknown, kPurityTypeReader, kPurityTypeUnknown),
          t(kPurityTypeUnknown, kPurityTypeUnknown, kPurityTypeUnknown),
          t(PurityType{.writes_local_variables = true}, kPurityTypePure,
            PurityType{.writes_local_variables = true}),
          t(PurityType{.writes_local_variables = true}, kPurityTypeUnknown,
            kPurityTypeUnknown),
      });
    }());
}  // namespace

bool operator==(const PurityType& a, const PurityType& b) {
  return a.writes_external_outputs == b.writes_external_outputs &&
         a.writes_local_variables == b.writes_local_variables &&
         a.reads_external_inputs == b.reads_external_inputs;
}

std::ostream& operator<<(std::ostream& os, const PurityType& value) {
  if (value == PurityType{})
    os << "pure";
  else if (value == kPurityTypeReader)
    os << "reader";
  else if (value == PurityType{.writes_local_variables = true})
    os << "local_variables";
  else
    os << "unknown";
  return os;
}

types::ObjectName NameForType(Type variant_type) {
  return std::visit(
      overload{[](const types::Void&) {
                 return types::ObjectName{LazyString{L"void"}};
               },
               [](const types::Bool&) {
                 return types::ObjectName{LazyString{L"bool"}};
               },
               [](const types::Number&) {
                 return types::ObjectName{LazyString{L"number"}};
               },
               [](const types::String&) {
                 return types::ObjectName{LazyString{L"string"}};
               },
               [](const types::Symbol&) {
                 return types::ObjectName{LazyString{L"symbol"}};
               },
               [](const types::ObjectName& object) { return object; },
               [](const types::Function&) {
                 return types::ObjectName{LazyString{L"function"}};
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

LazyString TypesToString(const std::vector<Type>& types) {
  return Concatenate(types | std::views::transform([](const Type& t) {
                       return LazyString{L"\""} + ToString(t) +
                              LazyString{L"\""};
                     }) |
                     Intersperse(LazyString{L", "}));
}

LazyString TypesToString(const std::unordered_set<Type>& types) {
  return TypesToString(std::vector<Type>(types.cbegin(), types.cend()));
}

LazyString ToString(const Type& type) {
  return std::visit(
      overload{
          [](const types::Void&) { return LazyString{L"void"}; },
          [](const types::Bool&) { return LazyString{L"bool"}; },
          [](const types::Number&) { return LazyString{L"number"}; },
          [](const types::String&) { return LazyString{L"string"}; },
          [](const types::Symbol&) { return LazyString{L"symbol"}; },
          [](const types::ObjectName& object) { return object.read(); },
          [](const types::Function& function_type) {
            return std::invoke([&] {
                     if (function_type.function_purity.writes_external_outputs)
                       return LazyString{L"FUNCTION"};
                     if (function_type.function_purity.reads_external_inputs)
                       return LazyString{L"FUNCtion"};
                     if (function_type.function_purity.writes_local_variables)
                       return LazyString{L"Function"};
                     return LazyString{L"function"};
                   }) +
                   LazyString{L"<"} + ToString(function_type.output.get()) +
                   LazyString{L"("} +
                   Concatenate(function_type.inputs |
                               std::views::transform(
                                   [](const Type& t) { return ToString(t); }) |
                               Intersperse(LazyString{L", "})) +
                   LazyString{L")>"};
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

std::vector<gc::Root<Value>> ObjectType::LookupField(
    const Identifier& name) const {
  auto range = fields_.equal_range(name);
  return container::MaterializeVector(
      std::ranges::subrange(range.first, range.second) |
      std::views::transform([](const auto& p) { return p.second.ToRoot(); }));
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
