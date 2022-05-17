#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::vm {

struct Value;
struct VMType;
class ObjectType;
class VMTypeObjectTypeName;

class Environment {
 public:
  using Namespace = std::vector<std::wstring>;

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

  const ObjectType* LookupObjectType(const VMTypeObjectTypeName& symbol);
  const VMType* LookupType(const std::wstring& symbol);
  void DefineType(language::NonNull<std::unique_ptr<ObjectType>> value);

  std::optional<language::gc::Root<Value>> Lookup(
      language::gc::Pool& pool, const Namespace& symbol_namespace,
      const std::wstring& symbol, VMType expected_type);

  // TODO(easy): Remove; switch all callers to the version that takes the
  // namespace.
  void PolyLookup(const std::wstring& symbol,
                  std::vector<language::gc::Root<Value>>* output);
  void PolyLookup(const Namespace& symbol_namespace, const std::wstring& symbol,
                  std::vector<language::gc::Root<Value>>* output);
  // Same as `PolyLookup` but ignores case and thus is much slower (runtime
  // complexity is linear to the total number of symbols defined);
  void CaseInsensitiveLookup(const Namespace& symbol_namespace,
                             const std::wstring& symbol,
                             std::vector<language::gc::Root<Value>>* output);
  void Define(const std::wstring& symbol, language::gc::Root<Value> value);
  void Assign(const std::wstring& symbol, language::gc::Root<Value> value);
  void Remove(const std::wstring& symbol, VMType type);

  void ForEachType(
      std::function<void(const std::wstring&, ObjectType&)> callback);
  void ForEach(
      std::function<void(const std::wstring&, const language::gc::Ptr<Value>&)>
          callback) const;
  void ForEachNonRecursive(
      std::function<void(const std::wstring&, const language::gc::Ptr<Value>&)>
          callback) const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ControlFrame>>>
  Expand() const;

 private:
  std::map<VMTypeObjectTypeName, language::NonNull<std::unique_ptr<ObjectType>>>
      object_types_;

  std::map<std::wstring, std::unordered_map<VMType, language::gc::Ptr<Value>>>
      table_;

  std::map<std::wstring, language::gc::Ptr<Environment>> namespaces_;

  // TODO: Consider whether the parent environment should itself be const?
  const std::optional<language::gc::Ptr<Environment>> parent_environment_ =
      std::nullopt;
};

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
    const afc::vm::Environment&);
}  // namespace afc::language::gc

#endif
