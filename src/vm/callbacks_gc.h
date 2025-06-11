// Extends //src/vm/callbacks.h with templated support for language::gc::Ptr
// values.
//
// Customers should just do this (in their CC file):
//
//     template <>
//     const types::ObjectName
//         VMTypeMapper<gc::Ptr<MyType>>::object_type_name{
//             IDENTIFIER_CONSTANT(L"MyType")};

#ifndef __AFC_VM_PUBLIC_CALLBACKS_GC_H__
#define __AFC_VM_PUBLIC_CALLBACKS_GC_H__

#include "src/language/gc.h"
#include "src/language/safe_types.h"
#include "src/vm/callbacks.h"

namespace afc::vm {
template <typename T>
struct VMTypeMapper<language::gc::Ptr<T>> {
  static language::gc::Ptr<T> get(Value& value) {
    return value.get_user_value<language::gc::Ptr<T>>(object_type_name).value();
  }

  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       language::gc::Ptr<T> value) {
    auto shared_value =
        language::MakeNonNullShared<language::gc::Ptr<T>>(value);
    return vm::Value::NewObject(
        pool, object_type_name, shared_value, [shared_value] {
          return std::vector<
              language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>{
              {shared_value->object_metadata()}};
        });
  }

  static language::gc::Root<Value> New(language::gc::Pool& pool,
                                       language::gc::Root<T> value) {
    return New(pool, value.ptr());
  }

  static const types::ObjectName object_type_name;
};
}  // namespace afc::vm
#endif