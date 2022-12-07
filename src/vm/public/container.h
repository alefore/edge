// Defines VM types corresponding to containers.
//
// To use it, define the vmtype of the container and of its nested type in your
// module:
//
//     template <>
//     const VMType
//     VMTypeMapper<NonNull<std::shared_ptr<std::vector<MyType>>>>::vmtype =
//         VMType::ObjectType(L"VectorMyType");
//
// Then initialize it in an environment:
//
//     ExportContainerType<std::vector<MyType>>(&environment);

#ifndef __AFC_VM_PUBLIC_VECTOR_H__
#define __AFC_VM_PUBLIC_VECTOR_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/safe_types.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm::container {

struct TraitsBase {
  static constexpr bool has_contains = false;
  static constexpr bool has_erase_by_index = false;
  static constexpr bool has_erase_by_element = false;
  static constexpr bool has_insert = false;
  static constexpr bool has_push_back = false;
};

template <typename T>
struct Traits {};

template <typename ValueType>
struct Traits<std::vector<ValueType>> : public TraitsBase {
  using ContainerPtr =
      language::NonNull<std::shared_ptr<std::vector<ValueType>>>;

  static ValueType Get(const std::vector<ValueType>& v, size_t index) {
    return v.at(index);
  }

  static constexpr bool has_erase_by_index = true;
  static void EraseByIndex(ContainerPtr v, int index) {
    v->erase(v->begin() + index);
  }

  static constexpr bool has_push_back = true;
  static void PushBack(ContainerPtr v, ValueType e) { v->emplace_back(e); }
};

template <typename ValueType>
struct Traits<std::set<ValueType>> : public TraitsBase {
  using ContainerPtr = language::NonNull<std::shared_ptr<std::set<ValueType>>>;

  static ValueType Get(const std::set<ValueType>& v, size_t index) {
    auto it = v.begin();
    std::advance(it, index);
    return *it;
  }

  static constexpr bool has_erase_by_element = true;
  static void EraseByElement(ContainerPtr v, ValueType t) { v->erase(t); }

  static constexpr bool has_contains = true;
  static bool Contains(ContainerPtr v, const ValueType& e) {
    return v->count(e) > 0;
  }

  static constexpr bool has_insert = true;
  static void Insert(ContainerPtr v, ValueType e) { v->insert(e); }
};

template <typename Container>
void Export(language::gc::Pool& pool, Environment& environment) {
  using T = Traits<Container>;
  using ContainerPtr = T::ContainerPtr;
  const VMType& vmtype = VMTypeMapper<ContainerPtr>::vmtype;
  auto object_type =
      language::MakeNonNullUnique<ObjectType>(vmtype.object_type);

  environment.Define(
      vmtype.object_type.read(),
      Value::NewFunction(pool, PurityType::kPure, {vmtype},
                         [&pool](std::vector<language::gc::Root<Value>> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               pool, vmtype.object_type,
                               language::MakeNonNullShared<Container>());
                         }));

  object_type->AddField(
      L"empty", vm::NewCallback(pool, PurityType::kPure,
                                [](ContainerPtr c) { return c->empty(); }));
  object_type->AddField(L"size", vm::NewCallback(pool, PurityType::kPure,
                                                 [](ContainerPtr v) -> int {
                                                   return v->size();
                                                 }));
  object_type->AddField(
      L"get",
      Value::NewFunction(
          pool, PurityType::kPure,
          {VMTypeMapper<typename Container::value_type>::vmtype, vmtype,
           VMType::Int()},
          [](std::vector<language::gc::Root<Value>> args,
             Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto v = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            int index = args[1].ptr()->get_int();
            if (index < 0 || static_cast<size_t>(index) >= v->size()) {
              return futures::Past(
                  language::Error(vmtype.ToString() + L": Index out of range " +
                                  std::to_wstring(index) + L" (size: " +
                                  std::to_wstring(v->size()) + L")"));
            }
            return futures::Past(language::Success(EvaluationOutput::New(
                VMTypeMapper<typename Container::value_type>::New(
                    trampoline.pool(), T::Get(v.value(), index)))));
          }));

  if constexpr (T::has_contains)
    object_type->AddField(
        L"contains", vm::NewCallback(pool, PurityType::kPure, T::Contains));

  if constexpr (T::has_erase_by_index) {
    object_type->AddField(
        L"erase", vm::NewCallback(pool, PurityType::kUnknown, T::EraseByIndex));
  }

  if constexpr (T::has_erase_by_element)
    object_type->AddField(L"erase", vm::NewCallback(pool, PurityType::kUnknown,
                                                    T::EraseByElement));

  if constexpr (T::has_insert)
    object_type->AddField(
        L"insert", vm::NewCallback(pool, PurityType::kUnknown, T::Insert));

  if constexpr (T::has_push_back)
    object_type->AddField(
        L"push_back", vm::NewCallback(pool, PurityType::kUnknown, T::PushBack));

  environment.DefineType(std::move(object_type));
}
}  // namespace afc::vm::container

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
