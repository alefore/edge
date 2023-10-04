#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

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
 public:
  Environment();
  explicit Environment(
      std::optional<language::gc::Ptr<Environment>> parent_environment);

  // Creates or returns an existing namespace inside parent with a given name.
  static language::gc::Root<Environment> NewNamespace(
      language::gc::Pool& pool, language::gc::Root<Environment> parent,
      std::wstring name);
  static std::optional<language::gc::Root<Environment>> LookupNamespace(
      language::gc::Root<Environment> source, const Namespace& name);

  // TODO: Implement proper garbage collection for the environment and get rid
  // of this method.
  void Clear();

  std::optional<language::gc::Ptr<Environment>> parent_environment() const;

  static language::gc::Root<Environment> NewDefault(language::gc::Pool& pool);

  const ObjectType* LookupObjectType(const types::ObjectName& symbol);
  const Type* LookupType(const std::wstring& symbol);
  void DefineType(language::gc::Ptr<ObjectType> value);

  std::optional<language::gc::Root<Value>> Lookup(
      language::gc::Pool& pool, const Namespace& symbol_namespace,
      const std::wstring& symbol, Type expected_type);

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
  std::map<types::ObjectName, language::gc::Ptr<ObjectType>> object_types_;

  std::map<std::wstring, std::unordered_map<Type, language::gc::Ptr<Value>>>
      table_;

  std::map<std::wstring, language::gc::Ptr<Environment>> namespaces_;

  // TODO: Consider whether the parent environment should itself be const?
  const std::optional<language::gc::Ptr<Environment>> parent_environment_ =
      std::nullopt;
};

template <>
const types::ObjectName VMTypeMapper<language::NonNull<
    std::shared_ptr<std::vector<std::wstring>>>>::object_type_name;

template <>
const types::ObjectName VMTypeMapper<language::NonNull<
    std::shared_ptr<std::set<std::wstring>>>>::object_type_name;

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ObjectMetadata>>> Expand(
    const afc::vm::Environment&);
}  // namespace afc::language::gc

#endif
