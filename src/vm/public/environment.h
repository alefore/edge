#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <map>
#include <memory>
#include <string>

namespace afc {
namespace vm {

using std::map;
using std::unique_ptr;
using std::wstring;

class Value;
class VMType;
class ObjectType;

class Environment {
 public:
  Environment();

  Environment(Environment* parent_environment);

  Environment* parent_environment() const { return parent_environment_; }

  static Environment* DefaultEnvironment();

  const ObjectType* LookupObjectType(const wstring& symbol);
  const VMType* LookupType(const wstring& symbol);
  void DefineType(const wstring& name, unique_ptr<ObjectType> value);

  Value* Lookup(const wstring& symbol);
  void Define(const wstring& symbol, unique_ptr<Value> value);

 private:
  map<wstring, unique_ptr<Value>>* table_;
  map<wstring, unique_ptr<ObjectType>>* object_types_;
  Environment* parent_environment_;
};

}  // namespace vm
}  // namespace afc

#endif
