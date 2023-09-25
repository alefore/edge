// `VMTypeMapper<T>` specializations enable callbacks that receive and/or return
// instances of the to be called from VM code.
//
// To receive instances of T, the Mapper implementation should define:
//
// * A `get` method that receives a `Value` instance and returns a `T` or a
//   `ValueOrError<T>`.
// * A `vmtype` method that specifies the type of the `Value` instance that the
//   `get` method expects.
//
// To allow callbacks to return a value `T`, the `VMTypeMapper<T>`
// implementation must define:
//
// * A `New` method that receives the value `T` and returns a `Value` instance
//   containing it.
#ifndef __AFC_VM_PUBLIC_CALLBACKS_H__
#define __AFC_VM_PUBLIC_CALLBACKS_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/error/value_or_error.h"
#include "src/language/function_traits.h"
#include "src/math/numbers.h"
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
struct VMTypeMapper<size_t> {
  static afc::language::ValueOrError<size_t> get(Value& value) {
    return afc::math::numbers::ToSizeT(value.get_number());
  }
  static language::gc::Root<Value> New(language::gc::Pool& pool, size_t value) {
    return Value::NewNumber(pool, afc::math::numbers::FromSizeT(value));
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<int> {
  static afc::language::ValueOrError<int> get(Value& value) {
    return afc::math::numbers::ToInt(value.get_number());
  }
  static language::gc::Root<Value> New(language::gc::Pool& pool, int value) {
    return Value::NewNumber(pool, afc::math::numbers::Number(value));
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<double> {
  static afc::language::ValueOrError<double> get(Value& value) {
    return afc::math::numbers::ToDouble(value.get_number());
  }
  static language::gc::Root<Value> New(language::gc::Pool& pool, double value) {
    return Value::NewNumber(pool, afc::math::numbers::FromDouble(value));
  }
  static const Type vmtype;
};

template <>
struct VMTypeMapper<math::numbers::Number> {
  static math::numbers::Number get(Value& value) { return value.get_number(); }
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       math::numbers::Number value) {
    return Value::NewNumber(pool, value);
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

template <typename T>
decltype(auto) UnwrapValueOrError(T& value) {
  return value;
}

template <typename T>
decltype(auto) UnwrapValueOrError(afc::language::ValueOrError<T>& value) {
  return std::get<T>(value);
}

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

template <typename Tuple, size_t Index = 0>
struct ErrorChecker {
  static std::optional<afc::language::Error> Check(const Tuple& tup) {
    using ElementType = std::tuple_element_t<Index, Tuple>;
    const auto& element = std::get<Index>(tup);
    if constexpr (afc::language::IsValueOrError<ElementType>::value) {
      if (afc::language::IsError(element))
        return std::optional<afc::language::Error>(
            std::get<afc::language::Error>(element));
    }
    return ErrorChecker<Tuple, Index + 1>::Check(tup);
  }
};

template <typename Tuple>
struct ErrorChecker<Tuple, std::tuple_size_v<Tuple>> {
  static std::optional<afc::language::Error> Check(const Tuple&) {
    return std::nullopt;
  }
};

template <typename... Args>
std::optional<afc::language::Error> ExtractFirstError(
    const std::tuple<Args...>& tuple) {
  return ErrorChecker<std::tuple<Args...>>::Check(tuple);
}

template <typename T>
struct UnwrapValueOrErrorType {
  using type = T;
};

template <typename T, typename ErrorType>
struct UnwrapValueOrErrorType<std::variant<T, ErrorType>> {
  using type = std::conditional_t<std::is_reference_v<T>, T, T&>;
};

template <typename... Args, std::size_t... Indices>
auto RemoveValueOrError(std::tuple<Args...>& args_tuple,
                        std::index_sequence<Indices...>) {
  return std::tuple<typename UnwrapValueOrErrorType<Args>::type...>(
      UnwrapValueOrError(std::get<Indices>(args_tuple))...);
}

template <typename... Args>
auto RemoveValueOrError(std::tuple<Args...>& args_tuple) {
  return RemoveValueOrError(args_tuple, std::index_sequence_for<Args...>{});
}

template <typename Callable, size_t... I>
futures::ValueOrError<EvaluationOutput> RunCallback(
    language::gc::Pool& pool, Callable& callback,
    std::vector<language::gc::Root<Value>> args, std::index_sequence<I...>) {
  using ft = language::function_traits<Callable>;
  CHECK_EQ(args.size(), std::tuple_size<typename ft::ArgTuple>::value);

  auto processed_args_or_error_tuple =
      std::make_tuple(ProcessArg<Callable, I>(args)...);
  if (std::optional<afc::language::Error> error =
          ExtractFirstError(processed_args_or_error_tuple);
      error.has_value())
    return futures::Past(
        afc::language::ValueOrError<EvaluationOutput>(error.value()));

  auto processed_args_tuple = RemoveValueOrError(processed_args_or_error_tuple);

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
        if constexpr (std::is_same<typename ft::ReturnType, void>::value) {
          return types::Void();
        } else if constexpr (!futures::is_future<
                                 typename ft::ReturnType>::value) {
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
