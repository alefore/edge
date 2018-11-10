#ifndef __AFC_VM_PUBLIC_VALUE_H__
#define __AFC_VM_PUBLIC_VALUE_H__

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "types.h"

namespace afc {
namespace vm {

using std::function;
using std::shared_ptr;
using std::string;
using std::vector;

class OngoingEvaluation;

struct Value {
  using Ptr = std::unique_ptr<Value>;

  Value(const VMType::Type& t) : type(t) {}
  Value(const VMType& t) : type(t) {}

  using Callback = std::function<void(std::vector<Ptr>, OngoingEvaluation*)>;
  static unique_ptr<Value> NewVoid();
  static unique_ptr<Value> NewBool(bool value);
  static unique_ptr<Value> NewInteger(int value);
  static unique_ptr<Value> NewDouble(double value);
  static unique_ptr<Value> NewString(wstring value);
  static unique_ptr<Value> NewObject(const wstring& name,
                                     const shared_ptr<void>& value);
  static unique_ptr<Value> NewFunction(std::vector<VMType> arguments,
                                       Callback callback);
  // Convenience wrapper.
  static unique_ptr<Value> NewFunction(
      std::vector<VMType> arguments,
      std::function<Ptr(std::vector<Ptr>)> callback);
  VMType type;

  bool boolean;
  int integer;
  double double_value;
  wstring str;
  Callback callback;
  shared_ptr<void> user_value;
};

std::ostream& operator<<(std::ostream& os, const Value& value);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_VALUE_H__