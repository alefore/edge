#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <string>

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
  Environment(std::shared_ptr<Environment> parent_environment);

  // Creates or returns an existing namespace inside parent with a given name.
  static std::shared_ptr<Environment> NewNamespace(
      std::shared_ptr<Environment> parent, std::wstring name);
  static std::shared_ptr<Environment> LookupNamespace(
      std::shared_ptr<Environment> source, const Namespace& name);

  // TODO: Implement proper garbage collection for the environment and get rid
  // of this method.
  void Clear();

  const std::shared_ptr<Environment>& parent_environment() const {
    return parent_environment_;
  }

  static const std::shared_ptr<Environment>& GetDefault();

  const ObjectType* LookupObjectType(const wstring& symbol);
  const VMType* LookupType(const wstring& symbol);
  void DefineType(const wstring& name, unique_ptr<ObjectType> value);

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
  void Assign(const wstring& symbol, unique_ptr<Value> value);
  void Remove(const wstring& symbol, VMType type);

  void ForEachType(std::function<void(const wstring&, ObjectType*)> callback);
  void ForEach(std::function<void(const wstring&, Value*)> callback);
  void ForEachNonRecursive(
      std::function<void(const wstring&, Value*)> callback);

 private:
  map<wstring, unique_ptr<ObjectType>> object_types_;
  map<wstring, std::unordered_map<VMType, unique_ptr<Value>>> table_;
  map<wstring, std::shared_ptr<Environment>> namespaces_;

  // TODO: Consider whether the parent environment should itself be const?
  const std::shared_ptr<Environment> parent_environment_;
};

}  // namespace afc::vm

#endif
