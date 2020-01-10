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

template <typename Tuple, size_t N>
void AddArgs(std::vector<VMType>* output) {
  if constexpr (N < std::tuple_size<Tuple>::value) {
    output->push_back(
        VMTypeMapper<typename std::tuple_element<N, Tuple>::type>::vmtype);
    AddArgs<Tuple, N + 1>(output);
  }
};

template <typename T>
struct function_traits : public function_traits<decltype(&T::operator())> {};

template <typename ClassType, typename R, typename... Args>
struct function_traits<R (ClassType::*)(Args...) const> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (&)(Args...)> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (*const)(Args...)> {
  using ReturnType = R;
  using ArgTuple = std::tuple<Args...>;
  static constexpr auto arity = sizeof...(Args);
};

template <typename Callable, size_t... I>
Value::Ptr RunCallback(Callable& callback, std::vector<Value::Ptr> args,
                       std::index_sequence<I...>) {
  using ft = function_traits<Callable>;
  CHECK_EQ(args.size(), std::tuple_size<typename ft::ArgTuple>::value);
  if constexpr (std::is_same<typename ft::ReturnType, void>::value) {
    callback(VMTypeMapper<typename std::tuple_element<
                 I, typename ft::ArgTuple>::type>::get(args.at(I).get())...);
    return Value::NewVoid();
  } else {
    return VMTypeMapper<typename ft::ReturnType>::New(callback(
        VMTypeMapper<typename std::tuple_element<
            I, typename ft::ArgTuple>::type>::get(args.at(I).get())...));
  }
}

template <typename Callable>
Value::Ptr NewCallback(Callable callback) {
  using ft = function_traits<Callable>;
  auto callback_wrapper = std::make_unique<Value>(VMType::FUNCTION);
  callback_wrapper->type.type_arguments.push_back(
      VMTypeMapper<typename ft::ReturnType>().vmtype);
  AddArgs<typename ft::ArgTuple, 0>(&callback_wrapper->type.type_arguments);
  callback_wrapper->callback = [callback = std::move(callback)](
                                   vector<Value::Ptr> args, Trampoline*) {
    return futures::ImmediateValue(EvaluationOutput::New(
        RunCallback(callback, std::move(args),
                    std::make_index_sequence<
                        std::tuple_size<typename ft::ArgTuple>::value>())));
  };
  return callback_wrapper;
}

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_CALLBACKS_H__
