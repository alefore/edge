#ifndef __AFC_VM_PUBLIC_CALLBACKS_H__
#define __AFC_VM_PUBLIC_CALLBACKS_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
struct Value;

template <class T>
struct VMTypeMapper {};

template <>
struct VMTypeMapper<void> {
  static Value::Ptr New() { return Value::NewVoid(); }

  static const VMType vmtype;
};

template <>
struct VMTypeMapper<bool> {
  static int get(Value* value) { return value->boolean; }

  static Value::Ptr New(bool value) { return Value::NewBool(value); }

  static const VMType vmtype;
};

template <>
struct VMTypeMapper<int> {
  static int get(Value* value) { return value->integer; }

  static Value::Ptr New(int value) { return Value::NewInteger(value); }

  static const VMType vmtype;
};

template <>
struct VMTypeMapper<double> {
  static double get(Value* value) { return value->double_value; }

  static Value::Ptr New(double value) { return Value::NewDouble(value); }

  static const VMType vmtype;
};

template <>
struct VMTypeMapper<wstring> {
  static wstring get(Value* value) { return std::move(value->str); }

  static Value::Ptr New(wstring value) { return Value::NewString(value); }

  static const VMType vmtype;
};

template <typename... Args>
struct AddArgs {
  // Terminates the recursion.
  static void Run(std::vector<VMType>*) {}
};

template <typename Arg0, typename... Args>
struct AddArgs<Arg0, Args...> {
  static void Run(std::vector<VMType>* output) {
    output->push_back(VMTypeMapper<Arg0>::vmtype);
    AddArgs<Args...>::Run(output);
  }
};

template <typename ReturnType, typename... A, size_t... I>
Value::Ptr RunCallback(const std::function<ReturnType(A...)>& callback,
                       std::vector<Value::Ptr> args,
                       std::index_sequence<I...>) {
  CHECK_EQ(args.size(), sizeof...(A));
  return VMTypeMapper<ReturnType>::New(
      callback(VMTypeMapper<A>::get(args.at(I).get())...));
}

template <typename... A, size_t... I>
Value::Ptr RunCallback(const std::function<void(A...)>& callback,
                       std::vector<Value::Ptr> args,
                       std::index_sequence<I...>) {
  CHECK_EQ(args.size(), sizeof...(A));
  callback(VMTypeMapper<A>::get(args.at(I).get())...);
  return Value::NewVoid();
}

template <typename ReturnType, typename... Args>
Value::Ptr NewCallback(std::function<ReturnType(Args...)> callback) {
  auto callback_wrapper = std::make_unique<Value>(VMType::FUNCTION);
  callback_wrapper->type.type_arguments.push_back(
      VMTypeMapper<ReturnType>().vmtype);
  AddArgs<Args...>::Run(&callback_wrapper->type.type_arguments);
  callback_wrapper->callback = [callback = std::move(callback)](
                                   vector<Value::Ptr> args, Trampoline*) {
    return futures::ImmediateValue(EvaluationOutput::New(RunCallback(
        callback, std::move(args), std::index_sequence_for<Args...>())));
  };
  return callback_wrapper;
}

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_CALLBACKS_H__
