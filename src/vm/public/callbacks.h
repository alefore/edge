#ifndef __AFC_VM_PUBLIC_CALLBACKS_H__
#define __AFC_VM_PUBLIC_CALLBACKS_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/function_traits.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace vm {

using std::unique_ptr;

class Expression;
class Value;

template <class T>
struct VMTypeMapper {};

template <>
struct VMTypeMapper<void> {
  static language::gc::Root<Value> New(language::gc::Pool& pool) {
    return Value::NewVoid(pool);
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<bool> {
  static int get(Value& value) { return value.get_bool(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool, bool value) {
    return Value::NewBool(pool, value);
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<int> {
  static int get(Value& value) { return value.get_int(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool, int value) {
    return Value::NewInt(pool, value);
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<double> {
  static double get(Value& value) { return value.get_double(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool, double value) {
    return Value::NewDouble(pool, value);
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<wstring> {
  static const wstring& get(const Value& value) { return value.get_string(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       const wstring& value) {
    return Value::NewString(pool, value);
  }
  static const Type vmtype;
};

template <typename T>
struct VMTypeMapper<language::NonNull<std::shared_ptr<T>>> {
  static language::NonNull<std::shared_ptr<T>> get(const Value& value) {
    return value.get_user_value<T>(object_type_name);
  }
  static language::gc::Root<Value> New(
      language::gc::Pool& pool, language::NonNull<std::shared_ptr<T>> value) {
    return Value::NewObject(pool, object_type_name, std::move(value));
  }
  static const types::ObjectName object_type_name;
};

template <typename>
struct GetVMType;

template <typename T>
struct GetVMType {
 public:
  static Type vmtype() { return vmtype_internal<VMTypeMapper<T>>(nullptr); }

 private:
  template <typename C>
  static Type vmtype_internal(decltype(C::vmtype)*) {
    return C::vmtype;
  };

  template <typename C>
  static Type vmtype_internal(decltype(C::object_type_name)*) {
    return C::object_type_name;
  };
};

template <typename Tuple, size_t N>
void AddArgs(std::vector<Type>* output) {
  if constexpr (N < std::tuple_size<Tuple>::value) {
    output->push_back(
        GetVMType<typename std::remove_const<typename std::remove_reference<
            typename std::tuple_element<N, Tuple>::type>::type>::type>::
            vmtype());
    AddArgs<Tuple, N + 1>(output);
  }
};

template <typename T>
struct ArgTupleMaker {
  using type = T;
};

template <typename T>
struct ArgTupleMaker<T&> {
  using type = std::reference_wrapper<T>;
};

// Given a callable and an index for its inputs, sets `type` to the appropriate
// `VMTypeMapper<>` implementation to use to convert a `vm::Value` to a value
// that the callable can receive (through the `VMTypeMapper`'s `get` method)
template <typename Callable, std::size_t Index>
struct VMTypeMapperResolver {
  using ft = language::function_traits<Callable>;
  using type = VMTypeMapper<typename std::remove_const<
      typename std::remove_reference<typename std::tuple_element<
          Index, typename ft::ArgTuple>::type>::type>::type>;
};

template <typename Callable, std::size_t Index, typename ArgsVector>
auto ProcessArg(const ArgsVector& args)
    -> ArgTupleMaker<decltype(VMTypeMapperResolver<Callable, Index>::type::get(
        args.at(Index).ptr().value()))>::type {
  return VMTypeMapperResolver<Callable, Index>::type::get(
      args.at(Index).ptr().value());
}

template <typename Callable, size_t... I>
futures::ValueOrError<EvaluationOutput> RunCallback(
    language::gc::Pool& pool, Callable& callback,
    std::vector<language::gc::Root<Value>> args, std::index_sequence<I...>) {
  using ft = language::function_traits<Callable>;
  CHECK_EQ(args.size(), std::tuple_size<typename ft::ArgTuple>::value);

  auto processed_args_tuple = std::make_tuple(ProcessArg<Callable, I>(args)...);

  // TODO(easy, 2022-05-13): Take a const ref to args.at(I).value().value() and
  // pass that to the VMTypeMapper<>::get functions, to ensure that they won't
  // modify the objects.
  if constexpr (std::is_same<typename ft::ReturnType, void>::value) {
    std::apply(callback, processed_args_tuple);
    return futures::Past(
        language::Success(EvaluationOutput::New(Value::NewVoid(pool))));
  } else if constexpr (!futures::is_future<typename ft::ReturnType>::value) {
    return futures::Past(language::Success(
        EvaluationOutput::New(VMTypeMapper<typename ft::ReturnType>::New(
            pool, std::apply(callback, processed_args_tuple)))));
  } else if constexpr (language::IsValueOrError<
                           typename ft::ReturnType::type>::value) {
    using NestedType = typename std::remove_reference<decltype(std::get<0>(
        std::declval<typename ft::ReturnType::type>()))>::type;
    return std::apply(callback, processed_args_tuple)
        .Transform([&pool](NestedType value) {
          if constexpr (std::is_same<NestedType, language::EmptyValue>::value) {
            return language::Success(
                EvaluationOutput::New(Value::NewVoid(pool)));
          } else {
            return language::Success(EvaluationOutput::New(
                (VMTypeMapper<NestedType>::New(pool, value))));
          }
        });
  } else {
    using NestedType = typename ft::ReturnType::type;
    return std::apply(callback, processed_args_tuple)
        .Transform([&pool](NestedType value) {
          if constexpr (std::is_same<NestedType, language::EmptyValue>::value) {
            return language::Success(
                EvaluationOutput::New(Value::NewVoid(pool)));
          } else {
            return language::Success(EvaluationOutput::New(
                (VMTypeMapper<NestedType>::New(pool, value))));
          }
        });
  }
}

template <typename Callable>
language::gc::Root<Value> NewCallback(language::gc::Pool& pool,
                                      PurityType purity_type,
                                      Callable callback) {
  using ft = language::function_traits<Callable>;
  std::vector<Type> type_arguments;
  AddArgs<typename ft::ArgTuple, 0>(&type_arguments);

  language::gc::Root<Value> callback_wrapper = Value::NewFunction(
      pool, purity_type,
      [&] {
        if constexpr (!futures::is_future<typename ft::ReturnType>::value) {
          return GetVMType<typename ft::ReturnType>::vmtype();
        } else if constexpr (std::is_same<typename ft::ReturnType::type,
                                          language::PossibleError>::value) {
          return types::Void{};
        } else if constexpr (language::IsValueOrError<
                                 typename ft::ReturnType::type>::value) {
          using NestedType =
              typename std::remove_reference<decltype(std::get<0>(
                  std::declval<typename ft::ReturnType::type>()))>::type;
          return GetVMType<NestedType>::vmtype();
        } else if constexpr (std::is_same<typename ft::ReturnType::type,
                                          language::EmptyValue>::value) {
          return types::Void{};
        } else {
          return GetVMType<typename ft::ReturnType::type>::vmtype();
        }
      }(),
      std::move(type_arguments),
      [callback = std::move(callback), &pool](
          vector<language::gc::Root<Value>> args, Trampoline&) {
        return RunCallback(
            pool, callback, std::move(args),
            std::make_index_sequence<
                std::tuple_size<typename ft::ArgTuple>::value>());
      });
  return callback_wrapper;
}

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_CALLBACKS_H__
