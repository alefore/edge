#ifndef __AFC_VM_PUBLIC_OPTIONAL_H__
#define __AFC_VM_PUBLIC_OPTIONAL_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/vm/public/value.h"
#include "src/vm/public/vm.h"

namespace afc {
namespace vm {

// Defines an optional type.
//
// To use it, define the vmtype of the optional<MyType> type in your module:
//
//     template <>
//     const VMType VMTypeMapper<std::optional<MyType>*>::vmtype =
//         VMType::ObjectType(L"OptionalMyType");
//
// Then initialize it in an environment:
//
//     VMTypeMapper<std::optional<MyType>*>::Export(&environment);
template <typename T>
struct VMTypeMapper<std::optional<T>*> {
  static std::optional<T>* get(Value* value) {
    return static_cast<std::optional<T>*>(value->user_value.get());
  }

  static language::gc::Root<Value> New(std::optional<T>* value) {
    // TODO: It's lame that we have to copy the values. :-/ We should find a way
    // to avoid that.
    auto value_copy = std::make_shared<std::optional<T>>(*value);
    std::shared_ptr<void> void_ptr(value_copy, value_copy.get());
    return Value::NewObject(vmtype.object_type, void_ptr);
  }

  static const VMType vmtype;

  static void Export(Environment* environment) {
    auto optional_type = std::make_unique<ObjectType>(vmtype);

    auto name = vmtype.object_type;
    environment->Define(
        name, Value::NewFunction(
                  {VMType::ObjectType(optional_type.get())},
                  [name](std::vector < language::gc::Root<Value> args) {
                    CHECK(args.empty());
                    return Value::NewObject(
                        name, std::make_shared<std::optional<T>>());
                  }));

    optional_type->AddField(
        L"has_value", vm::NewCallback(std::function<bool(std::optional<T>*)>(
                          [](std::optional<T>* v) { return v->has_value(); })));
    optional_type->AddField(
        L"value", vm::NewCallback(std::function<T(std::optional<T>*)>(
                      [](std::optional<T>* v) { return v->value(); })));
    optional_type->AddField(
        L"reset", vm::NewCallback(std::function<void(std::optional<T>*)>(
                      [](std::optional<T>* v) { *v = std::nullopt; })));
    optional_type->AddField(
        L"set", vm::NewCallback(std::function<void(std::optional<T>*, T)>(
                    [](std::optional<T>* o, T t) { *o = std::move(t); })));

    environment->DefineType(name, std::move(optional_type));
  }
};

}  // namespace vm
}  // namespace afc

#endif  // __AFC_VM_PUBLIC_OPTIONAL_H__
