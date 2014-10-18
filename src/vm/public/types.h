#ifndef __AFC_VM_PUBLIC_TYPES_H__
#define __AFC_VM_PUBLIC_TYPES_H__

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace afc {
namespace vm {

using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

class ObjectType;

struct VMType {
  enum Type {
    VM_VOID,
    VM_BOOLEAN,
    VM_INTEGER,
    VM_STRING,
    VM_SYMBOL,
    ENVIRONMENT,
    FUNCTION,
    OBJECT_TYPE,
  };

  VMType() : type(VM_VOID) {}
  VMType(const Type& t) : type(t) {}

  static const VMType& Void();
  static const VMType& Bool();
  static const VMType& Integer();
  static const VMType& String();

  static VMType ObjectType(ObjectType* type);
  static VMType ObjectType(const string& name);

  string ToString() const;

  Type type;
  vector<VMType> type_arguments;
  string object_type;
};

bool operator==(const VMType& lhs, const VMType& rhs);

class Value;

class ObjectType {
 public:
  ObjectType(const VMType& type);
  ObjectType(const string& type_name);
  ~ObjectType() {};

  const VMType& type() const { return type_; }
  string ToString() const { return type_.ToString(); }

  void AddField(const string& name, unique_ptr<Value> field);

  Value* LookupField(const string& name) const {
    auto it = fields_->find(name);
    return it == fields_->end() ? nullptr : it->second.get();
  }

 private:
  VMType type_;
  map<string, unique_ptr<Value>>* fields_;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_TYPES_H__
