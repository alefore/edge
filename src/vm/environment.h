#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
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
GHOST_TYPE_CONTAINER(Namespace, std::vector<std::wstring>)

class Environment {
  struct ConstructorAccessTag {};

  struct Data {
    std::map<std::wstring, std::unordered_map<Type, language::gc::Ptr<Value>>>
        table;

    std::map<std::wstring, language::gc::Ptr<Environment>> namespaces;
  };

  std::map<types::ObjectName, language::gc::Ptr<ObjectType>> object_types_;

  concurrent::Protected<Data> data_;

  // `parent_environment_` can't be const: `Assign` may want to recursively call
  // `Assign` on the parent (if the symbol we're assigning to is defined in the
  // parent).
  const std::optional<language::gc::Ptr<Environment>> parent_environment_ =
      std::nullopt;

 public:
  static language::gc::Root<Environment> New(language::gc::Pool& pool);
  static language::gc::Root<Environment> New(
      language::gc::Ptr<Environment> parent_environment);

  explicit Environment(ConstructorAccessTag);
  explicit Environment(ConstructorAccessTag,
                       language::gc::Ptr<Environment> parent_environment);

  // Creates or returns an existing namespace inside parent with a given name.
  static language::gc::Root<Environment> NewNamespace(
      language::gc::Ptr<Environment> parent, std::wstring name);
  static std::optional<language::gc::Root<Environment>> LookupNamespace(
      language::gc::Ptr<Environment> source, const Namespace& name);

  // TODO: Implement proper garbage collection for the environment and get rid
  // of this method.
  void Clear();

  std::optional<language::gc::Ptr<Environment>> parent_environment() const;

  const ObjectType* LookupObjectType(const types::ObjectName& symbol) const;
  const Type* LookupType(const std::wstring& symbol) const;
  void DefineType(language::gc::Ptr<ObjectType> value);

  std::optional<language::gc::Root<Value>> Lookup(
      language::gc::Pool& pool, const Namespace& symbol_namespace,
      const std::wstring& symbol, Type expected_type) const;

  void PolyLookup(const Namespace& symbol_namespace, const std::wstring& symbol,
                  std::vector<language::gc::Root<Value>>* output) const;
  // Same as `PolyLookup` but ignores case and thus is much slower (runtime
  // complexity is linear to the total number of symbols defined);
  void CaseInsensitiveLookup(
      const Namespace& symbol_namespace, const std::wstring& symbol,
      std::vector<language::gc::Root<Value>>* output) const;
  void Define(const std::wstring& symbol, language::gc::Root<Value> value);
  void Assign(const std::wstring& symbol, language::gc::Root<Value> value);
  void Remove(const std::wstring& symbol, Type type);

  void ForEachType(
      std::function<void(const types::ObjectName&, ObjectType&)> callback);
  void ForEach(
      std::function<void(const std::wstring&, const language::gc::Ptr<Value>&)>
          callback) const;
  void ForEachNonRecursive(
      std::function<void(const std::wstring&, const language::gc::Ptr<Value>&)>
          callback) const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;

 private:
  const Environment* FindNamespace(const Namespace& namespace_name) const;
};

template <>
const types::ObjectName VMTypeMapper<language::NonNull<
    std::shared_ptr<std::vector<std::wstring>>>>::object_type_name;

template <>
const types::ObjectName VMTypeMapper<language::NonNull<
    std::shared_ptr<std::set<std::wstring>>>>::object_type_name;

}  // namespace afc::vm

#endif
