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

template <typename ReturnType>
Value::Ptr RunCallback(std::function<ReturnType()> callback,
                       const vector<Value::Ptr>& args) {
  CHECK(args.empty());
  return VMTypeMapper<ReturnType>::New(callback());
}

template <>
Value::Ptr RunCallback(std::function<void()> callback,
                       const vector<Value::Ptr>& args);

template <typename ReturnType, typename A0>
Value::Ptr RunCallback(std::function<ReturnType(A0)> callback,
                       const vector<Value::Ptr>& args) {
  CHECK_EQ(args.size(), size_t(1));
  return VMTypeMapper<ReturnType>::New(
      callback(VMTypeMapper<A0>::get(args[0].get())));
}

template <typename ReturnType, typename A0>
Value::Ptr RunCallback(std::function<void(A0)> callback,
                       const vector<Value::Ptr>& args) {
  CHECK_EQ(args.size(), size_t(1));
  callback(VMTypeMapper<A0>::get(args[0].get()));
  return Value::NewVoid();
}

template <typename ReturnType, typename A0, typename A1>
Value::Ptr RunCallback(
    std::function<ReturnType(A0, A1)> callback, const vector<Value::Ptr>& args,
    typename std::enable_if<!std::is_void<ReturnType>::value>::type* = 0) {
  CHECK_EQ(args.size(), size_t(2));
  return VMTypeMapper<ReturnType>::New(
      callback(VMTypeMapper<A0>::get(args[0].get()),
               VMTypeMapper<A1>::get(args[1].get())));
}

template <typename ReturnType, typename A0, typename A1>
Value::Ptr RunCallback(std::function<void(A0, A1)> callback,
                       const vector<Value::Ptr>& args) {
  CHECK_EQ(args.size(), size_t(2));
  callback(VMTypeMapper<A0>::get(args[0].get()),
           VMTypeMapper<A1>::get(args[1].get()));
  return Value::NewVoid();
}

template <typename ReturnType, typename A0, typename A1, typename A2>
Value::Ptr RunCallback(
    std::function<ReturnType(A0, A1, A2)> callback,
    const vector<Value::Ptr>& args,
    typename std::enable_if<!std::is_void<ReturnType>::value>::type* = 0) {
  CHECK_EQ(args.size(), size_t(3));
  return VMTypeMapper<ReturnType>::New(
      callback(VMTypeMapper<A0>::get(args[0].get()),
               VMTypeMapper<A1>::get(args[1].get()),
               VMTypeMapper<A2>::get(args[2].get())));
}

template <typename ReturnType, typename A0, typename A1, typename A2>
Value::Ptr RunCallback(std::function<void(A0, A1, A2)> callback,
                       const vector<Value::Ptr>& args) {
  CHECK_EQ(args.size(), size_t(3));
  callback(VMTypeMapper<A0>::get(args[0].get()),
           VMTypeMapper<A1>::get(args[1].get()),
           VMTypeMapper<A2>::get(args[2].get()));
  return Value::NewVoid();
}

template <typename ReturnType, typename A0, typename A1, typename A2,
          typename A3>
Value::Ptr RunCallback(
    std::function<ReturnType(A0, A1, A2, A3)> callback,
    const vector<Value::Ptr>& args,
    typename std::enable_if<!std::is_void<ReturnType>::value>::type* = 0) {
  CHECK_EQ(args.size(), 4u);
  return VMTypeMapper<ReturnType>::New(
      callback(VMTypeMapper<A0>::get(args[0].get()),
               VMTypeMapper<A1>::get(args[1].get()),
               VMTypeMapper<A2>::get(args[2].get()),
               VMTypeMapper<A3>::get(args[3].get())));
}

template <typename ReturnType, typename A0, typename A1, typename A2,
          typename A3, typename A4>
Value::Ptr RunCallback(std::function<void(A0, A1, A2, A3, A4)> callback,
                       const vector<Value::Ptr>& args) {
  CHECK_EQ(args.size(), 5u);
  callback(VMTypeMapper<A0>::get(args[0].get()),
           VMTypeMapper<A1>::get(args[1].get()),
           VMTypeMapper<A2>::get(args[2].get()),
           VMTypeMapper<A3>::get(args[3].get()),
           VMTypeMapper<A4>::get(args[4].get()));
  return Value::NewVoid();
}

template <typename ReturnType, typename A0, typename A1, typename A2,
          typename A3>
Value::Ptr RunCallback(std::function<void(A0, A1, A2, A3)> callback,
                       const vector<Value::Ptr>& args) {
  CHECK_EQ(args.size(), 4u);
  callback(VMTypeMapper<A0>::get(args[0].get()),
           VMTypeMapper<A1>::get(args[1].get()),
           VMTypeMapper<A2>::get(args[2].get()),
           VMTypeMapper<A3>::get(args[3].get()));
  return Value::NewVoid();
}

template <typename ReturnType, typename... Args>
Value::Ptr NewCallback(std::function<ReturnType(Args...)> callback) {
  auto callback_wrapper = std::make_unique<Value>(VMType::FUNCTION);
  callback_wrapper->type.type_arguments.push_back(
      VMTypeMapper<ReturnType>().vmtype);
  AddArgs<Args...>::Run(&callback_wrapper->type.type_arguments);
  callback_wrapper->callback = [callback](vector<Value::Ptr> args,
                                          Trampoline*) {
    return futures::ImmediateValue(EvaluationOutput::New(
        RunCallback<ReturnType, Args...>(callback, args)));
  };
  return callback_wrapper;
}

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_CALLBACKS_H__
