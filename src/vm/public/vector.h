#ifndef __AFC_VM_PUBLIC_VECTOR_H__
#define __AFC_VM_PUBLIC_VECTOR_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/safe_types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
// Defines a vector type.
//
// To use it, define the vmtypes in your module:
//
//     template <>
//     const VMType VMTypeMapper<std::vector<MyType>*>::vmtype =
//         VMType::ObjectType(L"VectorMyType");
//
//     template <>
//     const VMType VMTypeMapper<std::unique_ptr<std::vector<MyType>>>::vmtype =
//         VMType::ObjectType(L"VectorMyType");
//
// Then initialize it in an environment:
//
//     VMTypeMapper<std::vector<MyType>*>::Export(&environment);
//
// You can then define a function that operates in vectors such as:
//
//    vm::NewCallback(
//        std::function<std::unique_ptr<std::vector<T>>(std::vector<T>*)>(...));
template <typename T>
struct VMTypeMapper<std::vector<T>*> {
  static std::vector<T>* get(Value& value) {
    return static_cast<std::vector<T>*>(value.get_user_value(vmtype).get());
  }

  static const VMType vmtype;

  static void Export(language::gc::Pool& pool, Environment& environment) {
    auto vector_type = language::MakeNonNullUnique<ObjectType>(vmtype);

    environment.Define(
        vector_type->type().object_type.read(),
        Value::NewFunction(
            pool, PurityType::kPure, {vmtype},
            [&pool](std::vector<language::gc::Root<Value>> args) {
              CHECK(args.empty());
              return Value::NewObject(
                  pool, vmtype.object_type,
                  language::MakeNonNullShared<std::vector<T>>());
            }));

    vector_type->AddField(L"empty", vm::NewCallback(pool, PurityType::kPure,
                                                    [](std::vector<T>* v) {
                                                      return v->empty();
                                                    }));
    vector_type->AddField(
        L"size",
        vm::NewCallback(pool, PurityType::kPure, [](std::vector<T>* v) {
          return static_cast<int>(v->size());
        }));
    vector_type->AddField(
        L"get",
        Value::NewFunction(
            pool, PurityType::kPure,
            {VMTypeMapper<T>::vmtype, vmtype, VMType::Int()},
            [](std::vector<language::gc::Root<Value>> args,
               Trampoline& trampoline)
                -> futures::ValueOrError<EvaluationOutput> {
              CHECK_EQ(args.size(), 2ul);
              auto* v = get(args[0].ptr().value());
              int index = args[1].ptr()->get_int();
              if (index < 0 || static_cast<size_t>(index) >= v->size()) {
                return futures::Past(language::Error(
                    vmtype.ToString() + L": Index out of range " +
                    std::to_wstring(index) + L" (size: " +
                    std::to_wstring(v->size()) + L")"));
              }
              return futures::Past(language::Success(EvaluationOutput::New(
                  VMTypeMapper<T>::New(trampoline.pool(), v->at(index)))));
            }));
    vector_type->AddField(L"erase",
                          vm::NewCallback(pool, PurityType::kUnknown,
                                          [](std::vector<T>* v, int i) {
                                            v->erase(v->begin() + i);
                                          }));
    vector_type->AddField(
        L"push_back",
        vm::NewCallback(pool, PurityType::kUnknown,
                        [](std::vector<T>* v, T e) { v->emplace_back(e); }));

    environment.DefineType(std::move(vector_type));
  }
};

// Allow safer construction than with VMTypeMapper<std::vector<T>>::New.
template <typename T>
struct VMTypeMapper<std::unique_ptr<std::vector<T>>> {
  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       std::unique_ptr<std::vector<T>> value) {
    // TODO(easy, 2022-05-27): Receive the value as a NonNull.
    return Value::NewObject(
        pool, vmtype.object_type,
        language::NonNull<std::shared_ptr<std::vector<T>>>::Unsafe(
            std::move(value)));
  }

  static const VMType vmtype;
};

}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
