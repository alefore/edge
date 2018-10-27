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

struct Value {
  Value(const VMType::Type& t) : type(t) {}
  Value(const VMType& t) : type(t) {}

  static unique_ptr<Value> NewVoid();
  static unique_ptr<Value> NewBool(bool value);
  static unique_ptr<Value> NewInteger(int value);
  static unique_ptr<Value> NewDouble(double value);
  static unique_ptr<Value> NewString(wstring value);
  static unique_ptr<Value> NewObject(const wstring& name,
                                     const shared_ptr<void>& value);

  VMType type;

  bool boolean;
  int integer;
  double double_value;
  wstring str;
  function<unique_ptr<Value>(vector<unique_ptr<Value>>)> callback;
  shared_ptr<void> user_value;
};

std::ostream& operator<<(std::ostream& os, const Value& value);

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_VALUE_H__