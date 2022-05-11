#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "src/language/gc.h"
#include "src/language/safe_types.h"

namespace afc::vm {

using std::map;
using std::unique_ptr;
using std::wstring;

struct Value;
struct VMType;
class ObjectType;

class Environment {
 public:
  using Namespace = std::vector<std::wstring>;

  Environment();
  explicit Environment(language::gc::Ptr<Environment> parent_environment);

  // Creates or returns an existing namespace inside parent with a given name.
  static language::gc::Root<Environment> NewNamespace(
      language::gc::Pool& pool, language::gc::Root<Environment> parent,
      std::wstring name);
  static language::gc::Root<Environment> LookupNamespace(
      language::gc::Root<Environment> source, const Namespace& name);

  // TODO: Implement proper garbage collection for the environment and get rid
  // of this method.
  void Clear();

  language::gc::Ptr<Environment> parent_environment() const;

  static language::gc::Root<Environment> NewDefault(language::gc::Pool& pool);

  const ObjectType* LookupObjectType(const wstring& symbol);
  const VMType* LookupType(const wstring& symbol);
  void DefineType(const std::wstring& name,
                  language::NonNull<std::unique_ptr<ObjectType>> value);

  std::unique_ptr<Value> Lookup(const Namespace& symbol_namespace,
                                const wstring& symbol, VMType expected_type);

  // TODO(easy): Remove; switch all callers to the version that takes the
  // namespace.
  void PolyLookup(const wstring& symbol, std::vector<Value*>* output);
  void PolyLookup(const Namespace& symbol_namespace, const wstring& symbol,
                  std::vector<Value*>* output);
  // Same as `PolyLookup` but ignores case and thus is much slower (runtime
  // complexity is linear to the total number of symbols defined);
  void CaseInsensitiveLookup(const Namespace& symbol_namespace,
                             const wstring& symbol,
                             std::vector<Value*>* output);
  void Define(const wstring& symbol,
              language::NonNull<std::unique_ptr<Value>> value);
  void Assign(const wstring& symbol,
              language::NonNull<std::unique_ptr<Value>> value);
  void Remove(const wstring& symbol, VMType type);

  void ForEachType(std::function<void(const wstring&, ObjectType&)> callback);
  void ForEach(std::function<void(const wstring&, Value&)> callback);
  void ForEachNonRecursive(
      std::function<void(const wstring&, Value&)> callback);

 private:
  std::map<std::wstring, language::NonNull<std::unique_ptr<ObjectType>>>
      object_types_;

  std::map<std::wstring, std::unordered_map<
                             VMType, language::NonNull<std::unique_ptr<Value>>>>
      table_;

  std::map<std::wstring, language::gc::Ptr<Environment>> namespaces_;

  // TODO: Consider whether the parent environment should itself be const?
  const language::gc::Ptr<Environment> parent_environment_;
};

}  // namespace afc::vm
namespace afc::language::gc {
std::vector<language::NonNull<std::shared_ptr<ControlFrame>>> Expand(
    const afc::vm::Environment&);
}  // namespace afc::language::gc

#endif
