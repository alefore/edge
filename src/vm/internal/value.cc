#include "../public/value.h"
#include "wstring.h"

namespace afc {
namespace vm {

/* static */ unique_ptr<Value> Value::NewVoid() {
  return unique_ptr<Value>(new Value(VMType::VM_VOID));
}

/* static */ unique_ptr<Value> Value::NewBool(bool value) {
  unique_ptr<Value> output(new Value(VMType::Bool()));
  output->boolean = value;
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewInteger(int value) {
  unique_ptr<Value> output(new Value(VMType::Integer()));
  output->integer = value;
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewString(const wstring& value) {
  unique_ptr<Value> output(new Value(VMType::String()));
  output->str = value;
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewObject(
    const wstring& name, const shared_ptr<void>& value) {
  unique_ptr<Value> output(new Value(VMType::ObjectType(name)));
  output->user_value = value;
  return std::move(output);
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  os << "[" << value.type.ToString() << "]";
  return os;
}

}  // namespace vm
}  // namespace afc
