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
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

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
  static futures::ValueOrError<afc::language::EmptyValue> EraseByIndex(
      ContainerPtr v, size_t index) {
    v->erase(v->begin() + index);
    return futures::Past(afc::language::Success());
  }

  static constexpr bool has_push_back = true;
  static void PushBack(ContainerPtr v, ValueType e) { v->emplace_back(e); }

  static constexpr bool has_set_at_index = true;
  static void SetAtIndex(ContainerPtr& v, size_t index, ValueType e) {
    v.value()[index] = e;
  }
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

  static constexpr bool has_set_at_index = false;
};

template <typename Container>
void Export(language::gc::Pool& pool, Environment& environment) {
  using T = Traits<Container>;
  using ContainerPtr = typename T::ContainerPtr;
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
      L"size", vm::NewCallback(pool, PurityType::kPure, [](ContainerPtr v) {
                 return v->size();
               }).ptr());
  object_type.ptr()->AddField(
      L"get",
      Value::NewFunction(
          pool, PurityType::kPure,
          GetVMType<typename Container::value_type>::vmtype(),
          {vmtype, types::Number{}},
          [object_type_name](std::vector<language::gc::Root<Value>> args,
                             Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto v = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            FUTURES_ASSIGN_OR_RETURN(int index, args[1].ptr()->get_int());
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

  if constexpr (T::has_set_at_index)
    object_type.ptr()->AddField(
        L"set",
        Value::NewFunction(
            pool, PurityType::kPure, types::Void{},
            {vmtype, types::Number{},
             GetVMType<typename Container::value_type>::vmtype()},
            [object_type_name](std::vector<language::gc::Root<Value>> args,
                               Trampoline& trampoline)
                -> futures::ValueOrError<EvaluationOutput> {
              CHECK_EQ(args.size(), 3ul);
              ContainerPtr v =
                  VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
              FUTURES_ASSIGN_OR_RETURN(int index, args[1].ptr()->get_int());
              if (index < 0 || static_cast<size_t>(index) >= v->size()) {
                return futures::Past(language::Error(
                    object_type_name.read() + L": Index out of range " +
                    std::to_wstring(index) + L" (size: " +
                    std::to_wstring(v->size()) + L")"));
              }
              auto value = VMTypeMapper<typename Container::value_type>::get(
                  args[2].ptr().value());
              if constexpr (afc::language::IsValueOrError<
                                decltype(value)>::value) {
                if (language::IsError(value))
                  return futures::Past(std::get<language::Error>(value));
                T::SetAtIndex(v, index, ValueOrDie(std::move(value)));
              } else {
                T::SetAtIndex(v, index, value);
              }
              return futures::Past(language::Success(
                  EvaluationOutput::New(Value::NewVoid(trampoline.pool()))));
            })
            .ptr());

  object_type.ptr()->AddField(
      L"filter",
      Value::NewFunction(
          pool, PurityType::kUnknown, vmtype,
          {vmtype,
           types::Function{
               .output = Type{types::Bool{}},
               .inputs =
                   {GetVMType<typename Container::value_type>::vmtype()}}},
          [](std::vector<language::gc::Root<Value>> args,
             Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto output_container = language::MakeNonNullShared<Container>();
            auto input = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            auto callback = args[1].ptr()->LockCallback();
            futures::ValueOrError<afc::language::EmptyValue> output =
                futures::Past(afc::language::EmptyValue());
            for (const auto& current_value : input.value()) {
              output =
                  std::move(output)
                      .Transform([&trampoline, callback,
                                  current_value](afc::language::EmptyValue) {
                        std::vector<language::gc::Root<vm::Value>> call_args;
                        call_args.push_back(
                            VMTypeMapper<typename Container::value_type>::New(
                                trampoline.pool(), current_value));
                        return callback(std::move(call_args), trampoline);
                      })
                      .Transform([output_container, current_value](
                                     EvaluationOutput callback_output) {
                        if constexpr (T::has_push_back) {
                          if (callback_output.value.ptr()->get_bool())
                            output_container->push_back(current_value);
                        } else {
                          if (callback_output.value.ptr()->get_bool())
                            output_container->insert(current_value);
                        }
                        return afc::language::Success();
                      });
            }
            return std::move(output).Transform(
                [&pool = trampoline.pool(),
                 output_container](afc::language::EmptyValue) {
                  return futures::Past(afc::language::Success(
                      EvaluationOutput::Return(VMTypeMapper<ContainerPtr>::New(
                          pool, std::move(output_container)))));
                });
          })
          .ptr());

  object_type.ptr()->AddField(
      L"ForEach",
      Value::NewFunction(
          pool, PurityType::kUnknown, types::Void{},
          {vmtype,
           types::Function{
               .output = Type{types::Void{}},
               .inputs =
                   {GetVMType<typename Container::value_type>::vmtype()}}},
          [](std::vector<language::gc::Root<Value>> args,
             Trampoline& trampoline)
              -> futures::ValueOrError<EvaluationOutput> {
            CHECK_EQ(args.size(), 2ul);
            auto input = VMTypeMapper<ContainerPtr>::get(args[0].ptr().value());
            auto callback = args[1].ptr()->LockCallback();
            futures::ValueOrError<afc::language::EmptyValue> output =
                futures::Past(afc::language::EmptyValue());
            for (const auto& current_value : input.value())
              output =
                  std::move(output)
                      .Transform([&trampoline, callback,
                                  current_value](afc::language::EmptyValue) {
                        std::vector<language::gc::Root<vm::Value>> call_args;
                        call_args.push_back(
                            VMTypeMapper<typename Container::value_type>::New(
                                trampoline.pool(), current_value));
                        return callback(std::move(call_args), trampoline);
                      })
                      .Transform([](EvaluationOutput) {
                        return futures::Past(afc::language::Success());
                      });
            return std::move(output).Transform(
                [&pool = trampoline.pool()](afc::language::EmptyValue) {
                  return futures::Past(afc::language::Success(
                      EvaluationOutput::Return(Value::NewVoid(pool))));
                });
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
