#ifndef __AFC_VM_PUBLIC_VECTOR_H__
#define __AFC_VM_PUBLIC_VECTOR_H__

#include <memory>
#include <type_traits>

#include <glog/logging.h>

#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace vm {

// Defines a vector type.
//
// To use it, define the vmtype of the vector<MyType> type in your module:
//
//     template <>
//     const VMType VMTypeMapper<std::vector<MyType>*>::vmtype =
//         VMType::ObjectType(L"VectorMyType");
//
// Then initialize it in an environment:
//
//     DefineVectorType<MyType>(&environment, L"VectorMyType");
template <typename T>
struct VMTypeMapper<std::vector<T>*> {
  static std::vector<T>* get(Value* value) {
    return static_cast<std::vector<T>*>(value->user_value.get());
  }

  static const VMType vmtype;

  static void Export(Environment* environment) {
    auto vector_type = std::make_unique<ObjectType>(vmtype);

    auto name = vmtype.object_type;
    environment->Define(
        name, Value::NewFunction({VMType::ObjectType(vector_type.get())},
                                 [name](std::vector<Value::Ptr> args) {
                                   CHECK(args.empty());
                                   return Value::NewObject(
                                       name,
                                       std::make_shared<std::vector<T>>());
                                 }));

    vector_type->AddField(L"size",
                          vm::NewCallback(std::function<int(std::vector<T>*)>(
                              [](std::vector<T>* v) { return v->size(); })));
    vector_type->AddField(
        L"get", vm::NewCallback(std::function<T(std::vector<T>*, int)>(
                    [](std::vector<T>* v, int i) { return v->at(i); })));
    vector_type->AddField(
        L"erase", vm::NewCallback(std::function<void(std::vector<T>*, int)>(
                      [](std::vector<T>* v, int i) {
                        return v->erase(v->begin() + i);
                      })));
    vector_type->AddField(
        L"push_back", vm::NewCallback(std::function<void(std::vector<T>*, T)>(
                          [](std::vector<T>* v, T e) { v->emplace_back(e); })));

    environment->DefineType(name, std::move(vector_type));
  }
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
