#ifndef __AFC_VM_PUBLIC_TYPES_H__
#define __AFC_VM_PUBLIC_TYPES_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::vm {
// Represents a single VM identifier within a namespace (e.g., `Buffer` or
// `lib`).
//
// TODO(easy, 2023-12-21): Find a way to assert that it only contains
// alphanumeric characters and that it isn't empty.
GHOST_TYPE(Identifier, std::wstring);

language::ValueOrError<Identifier> IdentifierOrError(
    language::lazy_string::LazyString);

// Return the identifier for "auto".
const Identifier& IdentifierAuto();
// Return the identifier for "include".
const Identifier& IdentifierInclude();

class ObjectType;

struct PurityType {
  bool writes_external_outputs = false;
  bool reads_external_inputs = false;
};

constexpr PurityType kPurityTypeUnknown =
    PurityType{.writes_external_outputs = true, .reads_external_inputs = true};
constexpr PurityType kPurityTypeReader =
    PurityType{.reads_external_inputs = true};
constexpr PurityType kPurityTypePure = PurityType{};

bool operator==(const PurityType& a, const PurityType& b);
std::ostream& operator<<(std::ostream& os, const PurityType& value);

// Return the purity type of an expression that depends on a set of purity
// types.
PurityType CombinePurityType(const std::vector<PurityType>& types);

namespace types {
struct Void {};
struct Bool {};
struct Number {};
struct String {};
struct Symbol {};
GHOST_TYPE(ObjectName, std::wstring);

// Function depends on `Type`, so we only forward declare it here.
struct Function;
bool operator==(const Void&, const Void&);
bool operator==(const Bool&, const Bool&);
bool operator==(const Number&, const Number&);
bool operator==(const String&, const String&);
bool operator==(const Symbol&, const Symbol&);
bool operator==(const Number&, const Number&);
bool operator==(const Function&, const Function&);
}  // namespace types

using Type =
    std::variant<types::Void, types::Bool, types::Number, types::String,
                 types::Symbol, types::ObjectName, types::Function>;

namespace types {
// Simple wrapper around NonNull<unique_ptr<>> that copies values.
template <typename T>
struct SimpleBox {
  SimpleBox(T t) : value(language::MakeNonNullUnique<T>(std::move(t))) {}

  SimpleBox(const SimpleBox<T>& other) : SimpleBox(other.get()) {}
  SimpleBox(SimpleBox<T>&& other) : value(std::move(other.value)) {}
  SimpleBox& operator=(SimpleBox<T> other) {
    value = std::move(other.value);
    return *this;
  }

  T& get() { return value.value(); }
  const T& get() const { return value.value(); }

 private:
  // Not const to enable move construction.
  language::NonNull<std::unique_ptr<T>> value;
};
template <typename T, typename U>
bool operator==(const SimpleBox<T>& t, const SimpleBox<U>& u) {
  return t.get() == u.get();
}

struct Function {
  SimpleBox<Type> output;
  std::vector<Type> inputs = {};
  PurityType function_purity = kPurityTypeUnknown;
};
}  // namespace types

types::ObjectName NameForType(Type variant_type);

language::lazy_string::LazyString ToString(const Type&);

language::lazy_string::LazyString TypesToString(const std::vector<Type>& types);
language::lazy_string::LazyString TypesToString(
    const std::unordered_set<Type>& types);

std::ostream& operator<<(std::ostream& os, const Type& value);

class Value;

class ObjectType {
 private:
  struct ConstructorAccessKey {};

 public:
  ObjectType(const Type& type, ConstructorAccessKey);

  static language::gc::Root<ObjectType> New(afc::language::gc::Pool& pool,
                                            Type type_name);

  const Type& type() const { return type_; }
  language::lazy_string::LazyString ToString() const {
    return vm::ToString(type_);
  }

  void AddField(const Identifier& name, language::gc::Ptr<Value> field);

  std::vector<language::NonNull<Value*>> LookupField(
      const Identifier& name) const;

  void ForEachField(std::function<void(const Identifier&, Value&)> callback);
  void ForEachField(
      std::function<void(const Identifier&, const Value&)> callback) const;

  std::vector<afc::language::NonNull<
      std::shared_ptr<afc::language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  Type type_;
  std::multimap<Identifier, language::gc::Ptr<Value>> fields_;
};

}  // namespace afc::vm

namespace std {
template <>
struct hash<afc::vm::PurityType> {
  size_t operator()(const afc::vm::PurityType& x) const;
};
template <>
struct hash<afc::vm::Type> {
  size_t operator()(const afc::vm::Type& x) const;
};
}  // namespace std

GHOST_TYPE_TOP_LEVEL(afc::vm::types::ObjectName);
GHOST_TYPE_TOP_LEVEL(afc::vm::Identifier);

#endif  // __AFC_VM_PUBLIC_TYPES_H__
