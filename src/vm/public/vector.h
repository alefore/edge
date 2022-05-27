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
// To use it, define the vmtype of the std::vector<MyType> and of MyType in your
// module:
//
//     template <>
//     const VMType
//     VMTypeMapper<NonNull<std::shared_ptr<std::vector<MyType>>>>::vmtype =
//         VMType::ObjectType(L"VectorMyType");
//
// Then initialize it in an environment:
//
//     ExportVectorType<MyType>(&environment);
template <typename T>
void ExportVectorType(language::gc::Pool& pool, Environment& environment) {
  const VMType& vmtype =
      VMTypeMapper<language::NonNull<std::shared_ptr<std::vector<T>>>>::vmtype;
  auto vector_type = language::MakeNonNullUnique<ObjectType>(vmtype);

  environment.Define(
      vector_type->type().object_type.read(),
      Value::NewFunction(pool, PurityType::kPure, {vmtype},
                         [&pool](std::vector<language::gc::Root<Value>> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               pool, vmtype.object_type,
                               language::MakeNonNullShared<std::vector<T>>());
                         }));

  vector_type->AddField(
      L"empty",
      vm::NewCallback(pool, PurityType::kPure,
                      [](language::NonNull<std::shared_ptr<std::vector<T>>> v) {
                        return v->empty();
                      }));
  vector_type->AddField(
      L"size",
      vm::NewCallback(pool, PurityType::kPure,
                      [](language::NonNull<std::shared_ptr<std::vector<T>>> v)
                          -> int { return v->size(); }));
  vector_type->AddField(
      L"get",
      Value::NewFunction(
          pool, PurityType::kPure,
          {VMTypeMapper<T>::vmtype, vmtype, VMType::Int()},
          [](std::vector<language::gc::Root<Value>> args,
             Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto v = VMTypeMapper<language::NonNull<
                std::shared_ptr<std::vector<T>>>>::get(args[0].ptr().value());
            int index = args[1].ptr()->get_int();
            if (index < 0 || static_cast<size_t>(index) >= v->size()) {
              return futures::Past(
                  language::Error(vmtype.ToString() + L": Index out of range " +
                                  std::to_wstring(index) + L" (size: " +
                                  std::to_wstring(v->size()) + L")"));
            }
            return futures::Past(language::Success(EvaluationOutput::New(
                VMTypeMapper<T>::New(trampoline.pool(), v->at(index)))));
          }));
  vector_type->AddField(
      L"erase",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](language::NonNull<std::shared_ptr<std::vector<T>>> v,
                         int i) { v->erase(v->begin() + i); }));
  vector_type->AddField(
      L"push_back",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](language::NonNull<std::shared_ptr<std::vector<T>>> v,
                         T e) { v->emplace_back(e); }));

  environment.DefineType(std::move(vector_type));
}
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
