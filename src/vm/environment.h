#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

#include "src/concurrent/protected.h"
#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/types.h"

namespace afc::vm {

class Value;
class ObjectType;
namespace types {
class ObjectName;
}
// Represents a namespace in the VM environment, where symbols can be defined.
// For example, a reference `lib::zk::Today` is actually the symbol "Today" in
// the namespace `Namespace{"lib", "zk"}`.
class Namespace
    : public language::GhostType<Namespace, std::vector<Identifier>> {};

struct UninitializedValue {};

class Environment {
  struct ConstructorAccessTag {};

  struct Data {
    std::map<Identifier,
             std::unordered_map<Type, std::variant<UninitializedValue,
                                                   language::gc::Ptr<Value>>>>
        table;

    std::map<Identifier, language::gc::Ptr<Environment>> namespaces;
  };

  std::map<types::ObjectName, language::gc::Ptr<ObjectType>> object_types_;

  concurrent::Protected<Data> data_;

  // The Environment instance pointed to by `parent_environment_` can't be
  // const: `Assign` may want to recursively call `Assign` on the parent
  // instance (if the symbol we're assigning to is defined in the parent).
  const std::optional<language::gc::Ptr<Environment>> parent_environment_ =
      std::nullopt;

 public:
  static language::gc::Root<Environment> New(language::gc::Pool& pool);
  // Creates a new environment that is a child of `parent_environment`. The new
  // gc::Root is created in the same gc::Pool as the `parent_environment`
  // pointer.
  static language::gc::Root<Environment> New(
      language::gc::Ptr<Environment> parent_environment);

  explicit Environment(ConstructorAccessTag);
  explicit Environment(ConstructorAccessTag,
                       language::gc::Ptr<Environment> parent_environment);

  // Creates or returns an existing namespace inside parent with a given name.
  static language::gc::Root<Environment> NewNamespace(
      language::gc::Ptr<Environment> parent, Identifier name);
  static std::optional<language::gc::Root<Environment>> LookupNamespace(
      language::gc::Ptr<Environment> source, const Namespace& name);

  // TODO: Implement proper garbage collection for the environment and get rid
  // of this method.
  void Clear();

  std::optional<language::gc::Ptr<Environment>> parent_environment() const;

  const ObjectType* LookupObjectType(const types::ObjectName& symbol) const;
  const Type* LookupType(const Identifier& symbol) const;
  void DefineType(language::gc::Ptr<ObjectType> value);

  struct LookupResult {
    enum class VariableScope { kLocal, kGlobal };
    VariableScope scope;
    Type type;
    std::variant<UninitializedValue, language::gc::Root<Value>> value;
  };

  // If a `LookupResult` is returned, its `value` is guaranteed to be
  // `gc::Root<Value>` (i.e., never returns an `UninitializedValue`).
  std::optional<LookupResult> Lookup(const Namespace& symbol_namespace,
                                     const Identifier& symbol,
                                     Type expected_type) const;

  std::vector<LookupResult> PolyLookup(const Namespace& symbol_namespace,
                                       const Identifier& symbol) const;

  // Same as `PolyLookup` but ignores case and thus is much slower (runtime
  // complexity is linear to the total number of symbols defined);
  void CaseInsensitiveLookup(
      const Namespace& symbol_namespace, const Identifier& symbol,
      std::vector<language::gc::Root<Value>>* output) const;
  void DefineUninitialized(const Identifier& symbol, const Type& type);
  void Define(const Identifier& symbol, language::gc::Root<Value> value);
  void Assign(const Identifier& symbol, language::gc::Root<Value> value);
  void Remove(const Identifier& symbol, Type type);

  void ForEachType(
      std::function<void(const types::ObjectName&, ObjectType&)> callback);
  void ForEach(
      std::function<void(
          const Identifier&,
          const std::variant<UninitializedValue, language::gc::Ptr<Value>>&)>
          callback) const;
  void ForEachNonRecursive(
      std::function<void(
          const Identifier&,
          const std::variant<UninitializedValue, language::gc::Ptr<Value>>&)>
          callback) const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  void PolyLookup(const Namespace& symbol_namespace, const Identifier& symbol,
                  LookupResult::VariableScope variable_scope,
                  std::vector<LookupResult>& output) const;

  const Environment* FindNamespace(const Namespace& namespace_name) const;
};

template <>
const types::ObjectName VMTypeMapper<language::NonNull<std::shared_ptr<
    concurrent::Protected<std::vector<Identifier>>>>>::object_type_name;

template <>
const types::ObjectName VMTypeMapper<language::NonNull<std::shared_ptr<
    concurrent::Protected<std::set<Identifier>>>>>::object_type_name;

}  // namespace afc::vm

#endif
