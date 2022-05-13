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
  static language::gc::Root<Value> New(language::gc::Pool& pool) {
    return Value::NewVoid(pool);
  }
  static const VMType vmtype;
};

template <>
struct VMTypeMapper<bool> {
  static int get(Value& value) { return value.get_bool(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool, bool value) {
    return Value::NewBool(pool, value);
  }
  static const VMType vmtype;
};

template <>
struct VMTypeMapper<int> {
  static int get(Value& value) { return value.get_int(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool, int value) {
    return Value::NewInt(pool, value);
  }
  static const VMType vmtype;
};

template <>
struct VMTypeMapper<double> {
  static double get(Value& value) { return value.get_double(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool, double value) {
    return Value::NewDouble(pool, value);
  }
  static const VMType vmtype;
};

template <>
struct VMTypeMapper<wstring> {
  static const wstring& get(const Value& value) { return value.get_string(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       const wstring& value) {
    return Value::NewString(pool, value);
  }
  static const VMType vmtype;
};

template <typename Tuple, size_t N>
void AddArgs(std::vector<VMType>* output) {
  if constexpr (N < std::tuple_size<Tuple>::value) {
    output->push_back(
        VMTypeMapper<typename std::remove_const<typename std::remove_reference<
            typename std::tuple_element<N, Tuple>::type>::type>::type>::vmtype);
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
language::gc::Root<Value> RunCallback(
    language::gc::Pool& pool, Callable& callback,
    std::vector<language::gc::Root<Value>> args, std::index_sequence<I...>) {
  using ft = function_traits<Callable>;
  CHECK_EQ(args.size(), std::tuple_size<typename ft::ArgTuple>::value);
  // TODO(easy, 2022-05-13): Take a const ref to args.at(I).value().value() and
  // pass that to the VMTypeMapper<>::get functions, to ensure that they won't
  // modify the objects.
  if constexpr (std::is_same<typename ft::ReturnType, void>::value) {
    callback(
        VMTypeMapper<typename std::remove_const<typename std::remove_reference<
            typename std::tuple_element<I, typename ft::ArgTuple>::
                type>::type>::type>::get(args.at(I).ptr().value())...);
    return Value::NewVoid(pool);
  } else {
    return VMTypeMapper<typename ft::ReturnType>::New(
        pool,
        callback(VMTypeMapper<typename std::remove_const<
                     typename std::remove_reference<typename std::tuple_element<
                         I, typename ft::ArgTuple>::type>::type>::type>::
                     get(args.at(I).ptr().value())...));
  }
}

template <typename Callable>
language::gc::Root<Value> NewCallback(
    language::gc::Pool& pool, Callable callback,
    VMType::PurityType purity = VMType::PurityType::kUnknown) {
  using ft = function_traits<Callable>;
  std::vector<VMType> type_arguments;
  type_arguments.push_back(VMTypeMapper<typename ft::ReturnType>().vmtype);
  AddArgs<typename ft::ArgTuple, 0>(&type_arguments);

  language::gc::Root<Value> callback_wrapper = Value::NewFunction(
      pool, std::move(type_arguments),
      [callback = std::move(callback), &pool](
          vector<language::gc::Root<Value>> args, Trampoline&) {
        language::gc::Root<Value> result =
            RunCallback(pool, callback, std::move(args),
                        std::make_index_sequence<
                            std::tuple_size<typename ft::ArgTuple>::value>());
        return futures::Past(
            language::Success(EvaluationOutput::New(std::move(result))));
      });
  callback_wrapper.ptr()->type.function_purity = purity;
  return callback_wrapper;
}

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_CALLBACKS_H__
