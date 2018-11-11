#include "../public/value.h"
#include "../public/vm.h"
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

/* static */ unique_ptr<Value> Value::NewDouble(double value) {
  unique_ptr<Value> output(new Value(VMType::Double()));
  output->double_value = value;
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewString(wstring value) {
  unique_ptr<Value> output(new Value(VMType::String()));
  output->str = std::move(value);
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewObject(
    const wstring& name, const shared_ptr<void>& value) {
  unique_ptr<Value> output(new Value(VMType::ObjectType(name)));
  output->user_value = value;
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewFunction(
    std::vector<VMType> arguments, Value::Callback callback) {
  std::unique_ptr<Value> output(new Value(VMType::FUNCTION));
  output->type.type_arguments = std::move(arguments);
  output->callback = std::move(callback);
  return std::move(output);
}

/* static */ unique_ptr<Value> Value::NewFunction(
    std::vector<VMType> arguments,
    std::function<Value::Ptr(std::vector<Value::Ptr>)> callback) {
  return NewFunction(arguments, [callback](
      std::vector<Ptr> args, Trampoline* trampoline) {
    trampoline->Return(callback(std::move(args)));
  });
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  os << "[" << value.type.ToString();
  if (value.type == VMType::Integer()) {
    os << ": " << value.integer;
  } else if (value.type == VMType::String()) {
    os << ": " << value.str;
  }
  os << "]";
  return os;
}

}  // namespace vm
}  // namespace afc
