// Defines VM types corresponding to containers.
//
// To use it, define the vmtype of the container and of its nested type in your
// module:
//
//     template <>
//     const types::ObjectName
//     VMTypeMapper<NonNull<std::shared_ptr<Protected<std::vector<MyType>>>>>
//         ::object_type_name =
//         types::ObjectName(L"VectorMyType");
//
// Then initialize it in an environment:
//
//     vm::container::Export<std::vector<MyType>>(pool, environment);

#ifndef __AFC_VM_PUBLIC_VECTOR_H__
#define __AFC_VM_PUBLIC_VECTOR_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/container.h"
#include "src/language/gc.h"
#include "src/language/gc_view.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"
#include "src/vm/expression.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

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
  using ContainerPtr = language::NonNull<
      std::shared_ptr<concurrent::Protected<std::vector<ValueType>>>>;

  static ValueType Get(const std::vector<ValueType>& v, size_t index) {
    return v.at(index);
  }

  static constexpr bool has_erase_by_index = true;
  static futures::ValueOrError<afc::language::EmptyValue> EraseByIndex(
      ContainerPtr ptr, size_t index) {
    ptr->lock([&](std::vector<ValueType>& v) { v.erase(v.begin() + index); });
    return futures::Past(afc::language::Success());
  }

  static constexpr bool has_push_back = true;
  static void PushBack(ContainerPtr ptr, ValueType e) {
    ptr->lock([&e](std::vector<ValueType>& v) { v.emplace_back(e); });
  }

  static constexpr bool has_set_at_index = true;
  static void SetAtIndex(std::vector<ValueType>& v, size_t index, ValueType e) {
    v[index] = std::move(e);
  }
};

template <typename ValueType>
struct Traits<std::set<ValueType>> : public TraitsBase {
  using ContainerPtr = language::NonNull<
      std::shared_ptr<concurrent::Protected<std::set<ValueType>>>>;

  static ValueType Get(const std::set<ValueType>& v, size_t index) {
    auto it = v.begin();
    std::advance(it, index);
    return *it;
  }

  static constexpr bool has_erase_by_element = true;
  static void EraseByElement(ContainerPtr ptr, ValueType t) {
    ptr->lock([&](std::set<ValueType>& c) { c.erase(t); });
  }

  static constexpr bool has_contains = true;
  static bool Contains(ContainerPtr ptr, const ValueType& e) {
    return ptr->lock([&](std::set<ValueType>& c) { return c.count(e) > 0; });
  }

  static constexpr bool has_insert = true;
  static void Insert(ContainerPtr ptr, ValueType e) {
    ptr->lock([&](std::set<ValueType>& c) { c.insert(e); });
  }

  static constexpr bool has_set_at_index = false;
};

template <typename NestedType>
struct NestedTypeTraits {
  static std::vector<
      language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand(const auto&) {
    return {};
  }
};

template <typename NestedType>
struct NestedTypeTraits<gc::Ptr<NestedType>> {
  static std::vector<
      language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand(const auto& value) {
    return language::container::MaterializeVector(value |
                                                  gc::view::ObjectMetadata);
  }
};

template <typename Container>
void Export(language::gc::Pool& pool, Environment& environment) {
  using T = Traits<Container>;
  using ContainerPtr = typename T::ContainerPtr;
  using language::lazy_string::LazyString;
  const types::ObjectName& object_type_name =
      VMTypeMapper<ContainerPtr>::object_type_name;
  const vm::Type vmtype = GetVMType<ContainerPtr>::vmtype();
  language::gc::Root<ObjectType> object_type = ObjectType::New(pool, vmtype);

  environment.Define(
      Identifier(object_type_name.read()),
      Value::NewFunction(
          pool, kPurityTypePure, vmtype, {},
          [&pool](std::vector<language::gc::Root<Value>> args) {
            CHECK(args.empty());
            auto value =
                language::MakeNonNullShared<concurrent::Protected<Container>>();
            return Value::NewObject(pool, object_type_name, value, [value]() {
              return value->lock([](auto& data) {
                return NestedTypeTraits<typename Container::value_type>::Expand(
                    data);
              });
            });
          }));

  object_type.ptr()->AddField(
      Identifier{LazyString{L"empty"}},
      vm::NewCallback(pool, kPurityTypePure, [](ContainerPtr ptr) {
        return ptr->lock([](Container& c) { return c.empty(); });
      }).ptr());
  object_type.ptr()->AddField(
      Identifier{LazyString{L"size"}},
      vm::NewCallback(pool, kPurityTypePure, [](ContainerPtr ptr) {
        return ptr->lock([](Container& c) { return c.size(); });
      }).ptr());
  object_type.ptr()->AddField(
      Identifier{LazyString{L"get"}},
      Value::NewFunction(
          pool, kPurityTypePure,
          GetVMType<typename Container::value_type>::vmtype(),
          {vmtype, types::Number{}},
          [object_type_name](std::vector<language::gc::Root<Value>> args,
                             Trampoline& trampoline)
              -> futures::ValueOrError<language::gc::Root<Value>> {
            CHECK_EQ(args.size(), 2ul);
            ContainerPtr ptr =
                VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            FUTURES_ASSIGN_OR_RETURN(int index, args[1].ptr()->get_int());
            return ptr->lock(
                [object_type_name, index, &trampoline](Container& c)
                    -> futures::ValueOrError<language::gc::Root<Value>> {
                  if (index < 0 || static_cast<size_t>(index) >= c.size()) {
                    return futures::Past(
                        language::Error{LazyString{object_type_name.read()} +
                                        LazyString{L": Index out of range "} +
                                        LazyString{std::to_wstring(index)} +
                                        LazyString{L" (size: "} +
                                        LazyString{std::to_wstring(c.size())} +
                                        LazyString{L")"}});
                  }
                  return futures::Past(language::Success(
                      VMTypeMapper<typename Container::value_type>::New(
                          trampoline.pool(), T::Get(c, index))));
                });
          })
          .ptr());

  if constexpr (T::has_set_at_index)
    object_type.ptr()->AddField(
        Identifier{LazyString{L"set"}},
        Value::NewFunction(
            pool, kPurityTypePure, types::Void{},
            {vmtype, types::Number{},
             GetVMType<typename Container::value_type>::vmtype()},
            [object_type_name](std::vector<language::gc::Root<Value>> args,
                               Trampoline& trampoline)
                -> futures::ValueOrError<language::gc::Root<Value>> {
              CHECK_EQ(args.size(), 3ul);
              ContainerPtr ptr =
                  VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
              FUTURES_ASSIGN_OR_RETURN(int index, args[1].ptr()->get_int());
              return ptr->lock(
                  [object_type_name, index, &args, &trampoline](Container& c)
                      -> futures::ValueOrError<language::gc::Root<Value>> {
                    if (index < 0 || static_cast<size_t>(index) >= c.size()) {
                      return futures::Past(language::Error{
                          LazyString{object_type_name.read()} +
                          LazyString{L": Index out of range "} +
                          LazyString{std::to_wstring(index)} +
                          LazyString{L" (size: "} +
                          LazyString{std::to_wstring(c.size())} +
                          LazyString{L")"}});
                    }
                    auto value =
                        VMTypeMapper<typename Container::value_type>::get(
                            args[2].ptr().value());
                    if constexpr (afc::language::IsValueOrError<
                                      decltype(value)>::value) {
                      if (language::IsError(value))
                        return futures::Past(std::get<language::Error>(value));
                      T::SetAtIndex(c, index,
                                    language::ValueOrDie(std::move(value)));
                    } else {
                      T::SetAtIndex(c, index, value);
                    }
                    return futures::Past(
                        language::Success(Value::NewVoid(trampoline.pool())));
                  });
            })
            .ptr());

  object_type.ptr()->AddField(
      Identifier{LazyString{L"filter"}},
      Value::NewFunction(
          pool, kPurityTypeUnknown, vmtype,
          {vmtype,
           types::Function{
               .output = Type{types::Bool{}},
               .inputs =
                   {GetVMType<typename Container::value_type>::vmtype()}}},
          [](std::vector<language::gc::Root<Value>> args,
             Trampoline& trampoline)
              -> futures::ValueOrError<language::gc::Root<Value>> {
            CHECK_EQ(args.size(), 2ul);
            Container output_container;
            auto ptr = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            const gc::Root<vm::Value> callback = std::move(args[1]);
            CHECK(callback.ptr()->IsFunction());
            futures::ValueOrError<afc::language::EmptyValue> output =
                futures::Past(afc::language::EmptyValue());
            ptr->lock([&output, &output_container, &trampoline,
                       &callback](Container& input) {
              for (const auto& current_value : input) {
                output =
                    std::move(output)
                        .Transform([&trampoline, callback,
                                    current_value](afc::language::EmptyValue) {
                          std::vector<language::gc::Root<vm::Value>> call_args;
                          call_args.push_back(
                              VMTypeMapper<typename Container::value_type>::New(
                                  trampoline.pool(), current_value));
                          return callback.ptr()->RunFunction(
                              std::move(call_args), trampoline);
                        })
                        .Transform([&output_container, current_value](
                                       gc::Root<Value> callback_output) {
                          if constexpr (T::has_push_back) {
                            if (callback_output.ptr()->get_bool())
                              output_container.push_back(current_value);
                          } else {
                            if (callback_output.ptr()->get_bool())
                              output_container.insert(current_value);
                          }
                          return afc::language::Success();
                        });
              }
            });
            return std::move(output).Transform(
                [&pool = trampoline.pool(),
                 output_container =
                     std::move(output_container)](afc::language::EmptyValue) {
                  return futures::Past(
                      afc::language::Success(VMTypeMapper<ContainerPtr>::New(
                          pool, language::MakeNonNullShared<
                                    concurrent::Protected<Container>>(
                                    std::move(output_container)))));
                });
          })
          .ptr());

  object_type.ptr()->AddField(
      Identifier{LazyString{L"ForEach"}},
      Value::NewFunction(
          pool, kPurityTypeUnknown, types::Void{},
          {vmtype,
           types::Function{
               .output = Type{types::Void{}},
               .inputs =
                   {GetVMType<typename Container::value_type>::vmtype()}}},
          [](std::vector<language::gc::Root<Value>> args,
             Trampoline& trampoline)
              -> futures::ValueOrError<language::gc::Root<Value>> {
            CHECK_EQ(args.size(), 2ul);
            auto ptr = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            const gc::Root<vm::Value> callback = std::move(args[1]);
            futures::ValueOrError<afc::language::EmptyValue> output =
                futures::Past(afc::language::EmptyValue());
            ptr->lock([&output, &trampoline, &callback](Container& input) {
              for (const auto& current_value : input)
                output =
                    std::move(output)
                        .Transform([&trampoline, callback,
                                    current_value](afc::language::EmptyValue) {
                          std::vector<language::gc::Root<vm::Value>> call_args;
                          call_args.push_back(
                              VMTypeMapper<typename Container::value_type>::New(
                                  trampoline.pool(), current_value));
                          return callback.ptr()->RunFunction(
                              std::move(call_args), trampoline);
                        })
                        .Transform([](gc::Root<Value>) {
                          return futures::Past(afc::language::Success());
                        });
            });
            return std::move(output).Transform(
                [&pool = trampoline.pool()](afc::language::EmptyValue) {
                  return futures::Past(
                      afc::language::Success(Value::NewVoid(pool)));
                });
          })
          .ptr());

  if constexpr (T::has_contains)
    object_type.ptr()->AddField(
        Identifier{LazyString{L"contains"}},
        vm::NewCallback(pool, kPurityTypePure, T::Contains).ptr());

  if constexpr (T::has_erase_by_index) {
    object_type.ptr()->AddField(
        Identifier{LazyString{L"erase"}},
        vm::NewCallback(pool, kPurityTypeUnknown, T::EraseByIndex).ptr());
  }

  if constexpr (T::has_erase_by_element)
    object_type.ptr()->AddField(
        Identifier{LazyString{L"erase"}},
        vm::NewCallback(pool, kPurityTypeUnknown, T::EraseByElement).ptr());

  if constexpr (T::has_insert)
    object_type.ptr()->AddField(
        Identifier{LazyString{L"insert"}},
        vm::NewCallback(pool, kPurityTypeUnknown, T::Insert).ptr());

  if constexpr (T::has_push_back)
    object_type.ptr()->AddField(
        Identifier{LazyString{L"push_back"}},
        vm::NewCallback(pool, kPurityTypeUnknown, T::PushBack).ptr());

  environment.DefineType(object_type.ptr());
}
}  // namespace afc::vm::container

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
