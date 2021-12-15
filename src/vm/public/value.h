#ifndef __AFC_VM_PUBLIC_VALUE_H__
#define __AFC_VM_PUBLIC_VALUE_H__

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "src/futures/futures.h"
#include "src/vm/public/types.h"
#include "src/vm/public/vm.h"

namespace afc::vm {

using std::function;
using std::shared_ptr;
using std::string;
using std::vector;

class Trampoline;
struct EvaluationOutput;

struct Value {
  using Ptr = std::unique_ptr<Value>;

  Value(const VMType::Type& t) : type(t) {}
  Value(const VMType& t) : type(t) {}

  using Callback = std::function<futures::Value<EvaluationOutput>(
      std::vector<Ptr>, Trampoline*)>;
  static unique_ptr<Value> NewVoid();
  static unique_ptr<Value> NewBool(bool value);
  static unique_ptr<Value> NewInteger(int value);
  static unique_ptr<Value> NewDouble(double value);
  static unique_ptr<Value> NewString(wstring value);
  static unique_ptr<Value> NewObject(std::wstring name,
                                     std::shared_ptr<void> value);
  static unique_ptr<Value> NewFunction(std::vector<VMType> arguments,
                                       Callback callback);
  // Convenience wrapper.
  static unique_ptr<Value> NewFunction(
      std::vector<VMType> arguments,
      std::function<Ptr(std::vector<Ptr>)> callback);

  bool IsVoid() const { return type.type == VMType::VM_VOID; };
  bool IsBool() const { return type.type == VMType::VM_BOOLEAN; };
  bool IsInteger() const { return type.type == VMType::VM_INTEGER; };
  bool IsDouble() const { return type.type == VMType::VM_DOUBLE; };
  bool IsString() const { return type.type == VMType::VM_STRING; };
  bool IsFunction() const { return type.type == VMType::FUNCTION; };
  bool IsObject() const { return type.type == VMType::OBJECT_TYPE; };

  VMType type;

  bool boolean;
  int integer;
  double double_value;
  wstring str;
  Callback callback;
  shared_ptr<void> user_value;
};

std::ostream& operator<<(std::ostream& os, const Value& value);

}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_VALUE_H__
