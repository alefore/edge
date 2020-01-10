#ifndef __AFC_VM_PUBLIC_SET_H__
#define __AFC_VM_PUBLIC_SET_H__

#include <glog/logging.h>

#include <memory>
#include <set>
#include <type_traits>

#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace vm {

// Defines a set type.
//
// To use it, define the vmtype of the std::set<MyType> type in your module:
//
//     template <>
//     const VMType VMTypeMapper<std::set<MyType>*>::vmtype =
//         VMType::ObjectType(L"SetMyType");
//
// Then initialize it in an environment:
//
//     VMTypeMapper<std::set<MyType>*>::Export(&environment);
template <typename T>
struct VMTypeMapper<std::set<T>*> {
  static std::set<T>* get(Value* value) {
    return static_cast<std::set<T>*>(value->user_value.get());
  }

  static const VMType vmtype;

  static void Export(Environment* environment) {
    auto set_type = std::make_unique<ObjectType>(vmtype);

    auto name = vmtype.object_type;
    environment->Define(
        name, Value::NewFunction({VMType::ObjectType(set_type.get())},
                                 [name](std::vector<Value::Ptr> args) {
                                   CHECK(args.empty());
                                   return Value::NewObject(
                                       name, std::make_shared<std::set<T>>());
                                 }));

    set_type->AddField(L"size",
                       vm::NewCallback(std::function<int(std::set<T>*)>(
                           [](std::set<T>* v) { return v->size(); })));
    set_type->AddField(L"empty",
                       vm::NewCallback(std::function<bool(std::set<T>*)>(
                           [](std::set<T>* v) { return v->empty(); })));
    set_type->AddField(
        L"contains", vm::NewCallback(std::function<bool(std::set<T>*, T)>(
                         [](std::set<T>* v, T e) { return v->count(e) > 0; })));
    set_type->AddField(L"get",
                       vm::NewCallback(std::function<T(std::set<T>*, int)>(
                           [](std::set<T>* v, int i) {
                             auto it = v->begin();
                             std::advance(it, i);
                             return *it;
                           })));

    set_type->AddField(L"erase",
                       vm::NewCallback(std::function<void(std::set<T>*, T)>(
                           [](std::set<T>* v, T t) { v->erase(t); })));
    set_type->AddField(L"insert",
                       vm::NewCallback(std::function<void(std::set<T>*, T)>(
                           [](std::set<T>* v, T e) { v->insert(e); })));

    environment->DefineType(name, std::move(set_type));
  }
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_SET_H__
