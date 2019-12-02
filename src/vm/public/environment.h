#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace afc {
namespace vm {

using std::map;
using std::unique_ptr;
using std::wstring;

struct Value;
struct VMType;
class ObjectType;

class Environment {
 public:
  Environment();

  Environment(Environment* parent_environment);

  Environment* parent_environment() const { return parent_environment_; }

  static Environment* GetDefault();

  const ObjectType* LookupObjectType(const wstring& symbol);
  const VMType* LookupType(const wstring& symbol);
  void DefineType(const wstring& name, unique_ptr<ObjectType> value);

  Value* Lookup(const wstring& symbol, VMType expected_type);
  void PolyLookup(const wstring& symbol, std::vector<Value*>* output);
  // Same as `PolyLookup` but ignores case and thus is much slower (runtime
  // complexity is linear to the total number of symbols defined);
  void CaseInsensitiveLookup(const wstring& symbol,
                             std::vector<Value*>* output);
  void Define(const wstring& symbol, unique_ptr<Value> value);
  void Assign(const wstring& symbol, unique_ptr<Value> value);

  void ForEachType(std::function<void(const wstring&, ObjectType*)> callback);
  void ForEach(std::function<void(const wstring&, Value*)> callback);

 private:
  map<wstring, unique_ptr<ObjectType>> object_types_;
  map<wstring, std::unordered_map<VMType, unique_ptr<Value>>> table_;
  Environment* parent_environment_;
};

}  // namespace vm
}  // namespace afc

#endif
