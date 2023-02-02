// Defines VM types corresponding to containers.
//
// To use it, define the vmtype of the container and of its nested type in your
// module:
//
//     template <>
//     const VMTypeObjectTypeName
//     VMTypeMapper<NonNull<std::shared_ptr<std::vector<MyType>>>>
//         ::object_type_name =
//         VMTypeObjectTypeName(L"VectorMyType");
//
// Then initialize it in an environment:
//
//     ExportContainerType<std::vector<MyType>>(&environment);

#ifndef __AFC_VM_PUBLIC_VECTOR_H__
#define __AFC_VM_PUBLIC_VECTOR_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

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
  const types::ObjectName& object_type_name =
      VMTypeMapper<ContainerPtr>::object_type_name;
  const vm::Type vmtype = GetVMType<ContainerPtr>::vmtype();
  language::gc::Root<ObjectType> object_type = ObjectType::New(pool, vmtype);

  environment.Define(
      object_type_name.read(),
      Value::NewFunction(pool, PurityType::kPure, vmtype, {},
                         [&pool](std::vector<language::gc::Root<Value>> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               pool, object_type_name,
                               language::MakeNonNullShared<Container>());
                         }));

  object_type.ptr()->AddField(
      L"empty", vm::NewCallback(pool, PurityType::kPure, [](ContainerPtr c) {
                  return c->empty();
                }).ptr());
  object_type.ptr()->AddField(
      L"size",
      vm::NewCallback(pool, PurityType::kPure, [](ContainerPtr v) -> int {
        return v->size();
      }).ptr());
  object_type.ptr()->AddField(
      L"get",
      Value::NewFunction(
          pool, PurityType::kPure,
          GetVMType<typename Container::value_type>::vmtype(),
          {vmtype, types::Int{}},
          [object_type_name](std::vector<language::gc::Root<Value>> args,
                             Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto v = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            int index = args[1].ptr()->get_int();
            if (index < 0 || static_cast<size_t>(index) >= v->size()) {
              return futures::Past(language::Error(
                  object_type_name.read() + L": Index out of range " +
                  std::to_wstring(index) + L" (size: " +
                  std::to_wstring(v->size()) + L")"));
            }
            return futures::Past(language::Success(EvaluationOutput::New(
                VMTypeMapper<typename Container::value_type>::New(
                    trampoline.pool(), T::Get(v.value(), index)))));
          })
          .ptr());

  if constexpr (T::has_contains)
    object_type.ptr()->AddField(
        L"contains",
        vm::NewCallback(pool, PurityType::kPure, T::Contains).ptr());

  if constexpr (T::has_erase_by_index) {
    object_type.ptr()->AddField(
        L"erase",
        vm::NewCallback(pool, PurityType::kUnknown, T::EraseByIndex).ptr());
  }

  if constexpr (T::has_erase_by_element)
    object_type.ptr()->AddField(
        L"erase",
        vm::NewCallback(pool, PurityType::kUnknown, T::EraseByElement).ptr());

  if constexpr (T::has_insert)
    object_type.ptr()->AddField(
        L"insert",
        vm::NewCallback(pool, PurityType::kUnknown, T::Insert).ptr());

  if constexpr (T::has_push_back)
    object_type.ptr()->AddField(
        L"push_back",
        vm::NewCallback(pool, PurityType::kUnknown, T::PushBack).ptr());

  environment.DefineType(object_type.ptr());
}
}  // namespace afc::vm::container

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
