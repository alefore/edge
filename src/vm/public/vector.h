#ifndef __AFC_VM_PUBLIC_VECTOR_H__
#define __AFC_VM_PUBLIC_VECTOR_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace vm {

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
  static std::vector<T>* get(Value* value) {
    CHECK_EQ(value->type, vmtype);
    return static_cast<std::vector<T>*>(value->user_value.get());
  }

  static const VMType vmtype;

  static void Export(Environment* environment) {
    auto vector_type = std::make_unique<ObjectType>(vmtype);

    auto name = vmtype.object_type;
    environment->Define(
        name,
        Value::NewFunction({vmtype}, [name](std::vector<Value::Ptr> args) {
          CHECK(args.empty());
          return Value::NewObject(name, std::make_shared<std::vector<T>>());
        }));

    vector_type->AddField(L"empty", vm::NewCallback([](std::vector<T>* v) {
                            return v->empty();
                          }));
    vector_type->AddField(L"size", vm::NewCallback([](std::vector<T>* v) {
                            return static_cast<int>(v->size());
                          }));
    vector_type->AddField(
        L"get",
        Value::NewFunction(
            {VMTypeMapper<T>::vmtype, vmtype, VMType::Integer()},
            [](std::vector<std::unique_ptr<Value>> args, Trampoline*) {
              CHECK_EQ(args.size(), 2ul);
              auto* v = get(args[0].get());
              CHECK(args[1]->IsInteger());
              int index = args[1]->integer;
              if (index < 0 || static_cast<size_t>(index) >= v->size()) {
                return futures::Past(EvaluationOutput::Abort(afc::editor::Error(
                    vmtype.ToString() + L": Index out of range " +
                    std::to_wstring(index) + L" (size: " +
                    std::to_wstring(v->size()) + L")")));
              }
              return futures::Past(
                  EvaluationOutput::New(VMTypeMapper<T>::New(v->at(index))));
            }));
    vector_type->AddField(L"erase",
                          vm::NewCallback([](std::vector<T>* v, int i) {
                            v->erase(v->begin() + i);
                          }));
    vector_type->AddField(
        L"push_back",
        vm::NewCallback([](std::vector<T>* v, T e) { v->emplace_back(e); }));

    environment->DefineType(name, std::move(vector_type));
  }
};

// Allow safer construction than with VMTypeMapper<std::vector<T>>::New.
template <typename T>
struct VMTypeMapper<std::unique_ptr<std::vector<T>>> {
  static Value::Ptr New(std::unique_ptr<std::vector<T>> value) {
    std::shared_ptr<void> void_ptr(value.release(), [](void* value) {
      delete static_cast<std::vector<T>*>(value);
    });
    return Value::NewObject(vmtype.object_type, void_ptr);
  }

  static const VMType vmtype;
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_VECTOR_H__
