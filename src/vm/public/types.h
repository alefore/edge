#ifndef __AFC_VM_PUBLIC_TYPES_H__
#define __AFC_VM_PUBLIC_TYPES_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::vm {

using std::map;
using std::unique_ptr;
using std::vector;
using std::wstring;

class ObjectType;

enum class PurityType {
  // Expression is completely pure: will always evaluate to the same value.
  kPure,

  // Expression doesn't have any side-effects, but depends on external
  // "environment" values; evaluating it repeatedly may yield different values.
  kReader,
  kUnknown
};

// Add definition to PurityType and remove this one.
constexpr PurityType PurityTypeWriter = PurityType::kUnknown;

std::ostream& operator<<(std::ostream& os, const PurityType& value);

// Given two purity type values, return the purity type of an expression that
// depends on both.
PurityType CombinePurityType(PurityType a, PurityType b);

namespace types {
struct Void {};
struct Bool {};
struct Int {};
struct String {};
struct Symbol {};
struct Double {};
GHOST_TYPE(ObjectName, std::wstring);

// Function depends on `Type`, so we only forward declare it here.
struct Function;
bool operator==(const Void&, const Void&);
bool operator==(const Bool&, const Bool&);
bool operator==(const Int&, const Int&);
bool operator==(const String&, const String&);
bool operator==(const Symbol&, const Symbol&);
bool operator==(const Double&, const Double&);
bool operator==(const Function&, const Function&);
}  // namespace types

using Type = std::variant<types::Void, types::Bool, types::Int, types::String,
                          types::Symbol, types::Double, types::ObjectName,
                          types::Function>;

namespace types {
struct Function {
  // The first element is the return type of the callback. Subsequent elements
  // are the types of the elements expected by the callback.
  vector<Type> type_arguments;
  PurityType function_purity = PurityType::kUnknown;
};
}  // namespace types

types::ObjectName NameForType(Type variant_type);

std::wstring ToString(const Type&);

wstring TypesToString(const std::vector<Type>& types);
wstring TypesToString(const std::unordered_set<Type>& types);

std::ostream& operator<<(std::ostream& os, const Type& value);

struct Value;

class ObjectType {
 private:
  struct ConstructorAccessKey {};

 public:
  ObjectType(const Type& type, ConstructorAccessKey);

  // TODO(easy, 2023-01-31): Convert all callers to the version that takes the
  // ObjectName.
  static language::gc::Root<ObjectType> New(afc::language::gc::Pool& pool,
                                            Type type_name);

  const Type& type() const { return type_; }
  std::wstring ToString() const { return vm::ToString(type_); }

  void AddField(const wstring& name, language::gc::Ptr<Value> field);

  Value* LookupField(const wstring& name) const;

  void ForEachField(std::function<void(const wstring&, Value&)> callback);
  void ForEachField(
      std::function<void(const wstring&, const Value&)> callback) const;

  std::vector<afc::language::NonNull<
      std::shared_ptr<afc::language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  Type type_;
  std::map<std::wstring, language::gc::Ptr<Value>> fields_;
};

}  // namespace afc::vm

namespace std {
template <>
struct hash<afc::vm::Type> {
  size_t operator()(const afc::vm::Type& x) const;
};
}  // namespace std

GHOST_TYPE_TOP_LEVEL(afc::vm::types::ObjectName);

#endif  // __AFC_VM_PUBLIC_TYPES_H__
