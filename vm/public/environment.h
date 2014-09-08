#ifndef __AFC_VM_PUBLIC_ENVIRONMENT_H__
#define __AFC_VM_PUBLIC_ENVIRONMENT_H__

#include <map>
#include <memory>
#include <string>

namespace afc {
namespace vm {

using std::map;
using std::string;
using std::unique_ptr;

class Value;
class VMType;
class ObjectType;

class Environment {
 public:
  Environment();

  Environment(Environment* parent_environment);

  Environment* parent_environment() const { return parent_environment_; }

  static Environment* DefaultEnvironment();

  const ObjectType* LookupObjectType(const string& symbol);
  const VMType* LookupType(const string& symbol);
  void DefineType(const string& name, unique_ptr<ObjectType> value);

  Value* Lookup(const string& symbol);
  void Define(const string& symbol, unique_ptr<Value> value);

 private:
  map<string, unique_ptr<Value>>* table_;
  map<string, unique_ptr<ObjectType>>* object_types_;
  Environment* parent_environment_;
};

}  // namespace vm
}  // namespace afc

#endif
