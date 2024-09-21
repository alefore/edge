#ifndef __AFC_VM_PUBLIC_OPTIONAL_H__
#define __AFC_VM_PUBLIC_OPTIONAL_H__

#include <glog/logging.h>

#include <memory>
#include <type_traits>

#include "src/language/error/value_or_error.h"
#include "src/language/gc.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/language/safe_types.h"
#include "src/vm/value.h"
#include "src/vm/vm.h"

namespace afc::vm::optional {

// Defines an optional type.
//
// To use it, define the `object_type_name` of the
// `NonNull<std::shared_ptr<std::optional<MyType>>>` type in your module:
//
//     template <>
//     const types::ObjectType
//     VMTypeMapper<NonNull<std::shared_ptr<std::optional<MyType>>>>
//         ::object_type_name =
//         types::ObjectName(L"OptionalMyType");
//
// You'll probably want to surface it in header files (if you expect to define
// functions that receive this type).
//
// Then initialize it in an environment:
//
//     vm::optional::Export<MyType>::Export(&environment);
template <typename T>
void Export(language::gc::Pool& pool, Environment& environment) {
  using FullType = language::NonNull<std::shared_ptr<std::optional<T>>>;
  const types::ObjectName& object_type_name =
      VMTypeMapper<FullType>::object_type_name;
  const vm::Type vmtype = GetVMType<FullType>::vmtype();
  language::gc::Root<ObjectType> object_type = ObjectType::New(pool, vmtype);

  environment.Define(
      object_type_name.read(),
      Value::NewFunction(pool, kPurityTypePure, vmtype, {},
                         [&pool](std::vector<language::gc::Root<Value>> args) {
                           CHECK(args.empty());
                           return Value::NewObject(
                               pool, object_type_name,
                               language::MakeNonNullShared<std::optional<T>>());
                         }));
  object_type.ptr()->AddField(
      Identifier{language::lazy_string::NonEmptySingleLine{
          language::lazy_string::SingleLine{
              language::lazy_string::LazyString{L"has_value"}}}},
      vm::NewCallback(pool, kPurityTypePure, [](FullType v) {
        return v->has_value();
      }).ptr());
  object_type.ptr()->AddField(
      Identifier{language::lazy_string::NonEmptySingleLine{
          language::lazy_string::SingleLine{
              language::lazy_string::LazyString{L"value"}}}},
      vm::NewCallback(pool, kPurityTypePure,
                      [](FullType v) -> language::ValueOrError<T> {
                        if (v->has_value()) return v->value();
                        return language::Error{
                            language::lazy_string::LazyString{
                                L"Optional value has no value."}};
                      })
          .ptr());
  object_type.ptr()->AddField(
      Identifier{language::lazy_string::NonEmptySingleLine{
          language::lazy_string::SingleLine{
              language::lazy_string::LazyString{L"reset"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, [](FullType v) {
        v.value() = std::nullopt;
      }).ptr());
  object_type.ptr()->AddField(
      Identifier{language::lazy_string::NonEmptySingleLine{
          language::lazy_string::SingleLine{
              language::lazy_string::LazyString{L"set"}}}},
      vm::NewCallback(pool, kPurityTypeUnknown, [](FullType o, T t) {
        o.value() = std::move(t);
      }).ptr());

  environment.DefineType(object_type.ptr());
}

}  // namespace afc::vm::optional

#endif  // __AFC_VM_PUBLIC_OPTIONAL_H__
