#ifndef __AFC_VM_PUBLIC_TYPES_H__
#define __AFC_VM_PUBLIC_TYPES_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace afc {
namespace vm {

using std::map;
using std::unique_ptr;
using std::vector;
using std::wstring;

class ObjectType;

struct VMType {
  enum Type {
    VM_VOID,
    VM_BOOLEAN,
    VM_INTEGER,
    VM_STRING,
    VM_SYMBOL,
    VM_DOUBLE,
    ENVIRONMENT,
    FUNCTION,
    OBJECT_TYPE,
  };

  enum class PurityType { kPure, kUnknown };

  VMType() : type(VM_VOID) {}
  VMType(const Type& t) : type(t) {}

  static const VMType& Void();
  static const VMType& Bool();
  static const VMType& Integer();
  static const VMType& String();
  static const VMType& Double();

  static VMType ObjectType(ObjectType* type);
  static VMType ObjectType(const wstring& name);

  static VMType Function(vector<VMType> arguments,
                         PurityType function_purity = PurityType::kUnknown);

  wstring ToString() const;

  Type type;
  // When type is FUNCTION, this contains the types. The first element is the
  // return type of the callback. Subsequent elements are the types of the
  // elements expected by the callback.
  vector<VMType> type_arguments;
  PurityType function_purity = PurityType::kUnknown;

  wstring object_type;
};

wstring TypesToString(const std::vector<VMType>& types);
wstring TypesToString(const std::unordered_set<VMType>& types);

bool operator==(const VMType& lhs, const VMType& rhs);

struct Value;

class ObjectType {
 public:
  ObjectType(const VMType& type);
  ObjectType(const wstring& type_name);
  ~ObjectType(){};

  const VMType& type() const { return type_; }
  wstring ToString() const { return type_.ToString(); }

  void AddField(const wstring& name, unique_ptr<Value> field);

  Value* LookupField(const wstring& name) const {
    auto it = fields_.find(name);
    return it == fields_.end() ? nullptr : it->second.get();
  }

  void ForEachField(std::function<void(const wstring&, Value*)> callback);

 private:
  VMType type_;
  map<wstring, unique_ptr<Value>> fields_;
};

}  // namespace vm
}  // namespace afc

namespace std {
template <>
struct hash<afc::vm::VMType> {
  size_t operator()(const afc::vm::VMType& x) const;
};
}  // namespace std

#endif  // __AFC_VM_PUBLIC_TYPES_H__
