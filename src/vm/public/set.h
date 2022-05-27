#ifndef __AFC_VM_PUBLIC_SET_H__
#define __AFC_VM_PUBLIC_SET_H__

#include <glog/logging.h>

#include <memory>
#include <set>
#include <type_traits>

#include "src/language/safe_types.h"
#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc::language::gc {
class Pool;
}
namespace afc::vm {
// Defines a set type.
//
// To use it, define the vmtype of the std::set<MyType> type and of MyType in
// your module:
//
//     template <>
//     const VMType
//     VMTypeMapper<NonNull<std::shared_ptr<std::set<MyType>>>>::vmtype =
//         VMType::ObjectType(L"SetMyType");
//
// Then initialize it in an environment:
//
//     ExportSetType(&environment);
template <typename T>
void ExportSetType(language::gc::Pool& pool, Environment& environment) {
  const VMType& vmtype =
      VMTypeMapper<language::NonNull<std::shared_ptr<std::set<T>>>>::vmtype;
  auto set_type = language::MakeNonNullUnique<ObjectType>(vmtype);

  environment.Define(
      vmtype.object_type.read(),
      Value::NewFunction(pool, PurityType::kPure, {set_type->type()},
                         [&pool](std::vector<language::gc::Root<Value>> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               pool, vmtype.object_type,
                               language::MakeNonNullShared<std::set<T>>());
                         }));

  set_type->AddField(
      L"size",
      vm::NewCallback(pool, PurityType::kPure,
                      [](language::NonNull<std::shared_ptr<std::set<T>>> v)
                          -> int { return v->size(); }));
  set_type->AddField(
      L"empty",
      vm::NewCallback(pool, PurityType::kPure,
                      [](language::NonNull<std::shared_ptr<std::set<T>>> v) {
                        return v->empty();
                      }));
  set_type->AddField(
      L"contains",
      vm::NewCallback(pool, PurityType::kPure,
                      [](language::NonNull<std::shared_ptr<std::set<T>>> v,
                         T e) { return v->count(e) > 0; }));
  set_type->AddField(
      L"get", vm::NewCallback(
                  pool, PurityType::kPure,
                  [](language::NonNull<std::shared_ptr<std::set<T>>> v, int i) {
                    auto it = v->begin();
                    std::advance(it, i);
                    return *it;
                  }));

  set_type->AddField(
      L"erase",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](language::NonNull<std::shared_ptr<std::set<T>>> v,
                         T t) { v->erase(t); }));
  set_type->AddField(
      L"insert",
      vm::NewCallback(pool, PurityType::kUnknown,
                      [](language::NonNull<std::shared_ptr<std::set<T>>> v,
                         T e) { v->insert(e); }));

  environment.DefineType(std::move(set_type));
}
}  // namespace afc::vm

#endif  // __AFC_VM_PUBLIC_SET_H__
