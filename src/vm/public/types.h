#ifndef __AFC_VM_PUBLIC_TYPES_H__
#define __AFC_VM_PUBLIC_TYPES_H__

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "src/language/gc.h"
#include "src/language/ghost_type.h"
#include "src/language/safe_types.h"
#include "src/language/wstring.h"

namespace afc::vm {

using std::map;
using std::unique_ptr;
using std::vector;
using std::wstring;

class ObjectType;

GHOST_TYPE(VMTypeObjectTypeName, std::wstring);

struct VMType {
  enum class Type {
    kVoid,
    kBool,
    kInt,
    kString,
    kSymbol,
    kDouble,
    kFunction,
    kObject
  };

  enum class PurityType { kPure, kUnknown };

  VMType() = default;
  explicit VMType(const Type& t) : type(t) {}

  static const VMType& Void();
  static const VMType& Bool();
  static const VMType& Int();
  static const VMType& String();
  static const VMType& Symbol();
  static const VMType& Double();

  static VMType ObjectType(VMTypeObjectTypeName name);

  static VMType Function(vector<VMType> arguments,
                         PurityType function_purity = PurityType::kUnknown);

  wstring ToString() const;

  Type type = Type::kVoid;
  // When type is FUNCTION, this contains the types. The first element is the
  // return type of the callback. Subsequent elements are the types of the
  // elements expected by the callback.
  vector<VMType> type_arguments;
  PurityType function_purity = PurityType::kUnknown;

  VMTypeObjectTypeName object_type = VMTypeObjectTypeName(L"");
};

wstring TypesToString(const std::vector<VMType>& types);
wstring TypesToString(const std::unordered_set<VMType>& types);

bool operator==(const VMType& lhs, const VMType& rhs);
std::ostream& operator<<(std::ostream& os, const VMType& value);

struct Value;

class ObjectType {
 public:
  ObjectType(const VMType& type);
  ObjectType(VMTypeObjectTypeName type_name);
  ~ObjectType(){};

  const VMType& type() const { return type_; }
  wstring ToString() const { return type_.ToString(); }

  void AddField(const wstring& name, language::gc::Root<Value> field);

  Value* LookupField(const wstring& name) const {
    auto it = fields_.find(name);
    return it == fields_.end() ? nullptr : &it->second.ptr().value();
  }

  void ForEachField(std::function<void(const wstring&, Value&)> callback);

 private:
  VMType type_;
  std::map<std::wstring, language::gc::Root<Value>> fields_;
};

}  // namespace afc::vm

namespace std {
template <>
struct hash<afc::vm::VMType> {
  size_t operator()(const afc::vm::VMType& x) const;
};
}  // namespace std

GHOST_TYPE_TOP_LEVEL(afc::vm::VMTypeObjectTypeName);

#endif  // __AFC_VM_PUBLIC_TYPES_H__
