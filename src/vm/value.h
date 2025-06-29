#ifndef __AFC_VM_PUBLIC_VALUE_H__
#define __AFC_VM_PUBLIC_VALUE_H__

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "src/futures/futures.h"
#include "src/language/gc.h"
#include "src/language/gc_util.h"
#include "src/math/numbers.h"
#include "src/vm/types.h"

namespace afc::vm {
class Trampoline;

class Value {
  using Callback =
      std::function<futures::ValueOrError<language::gc::Root<Value>>(
          std::vector<language::gc::Root<Value>>, Trampoline&)>;

  using ExpandCallback = std::function<std::vector<
      language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>()>;

  class ConstructorAccessTag {
    ConstructorAccessTag() {};
    friend Value;
  };

  // TODO(2025-05-30, trivial): We could remove this? Nothing seems to use it.
  language::gc::Pool& pool_;

  Type type_;

  struct ObjectInstance {
    language::NonNull<std::shared_ptr<void>> value;
  };

  std::variant<bool, math::numbers::Number, language::lazy_string::LazyString,
               Identifier, ObjectInstance, Callback>
      value_;

  ExpandCallback expand_callback_;

 public:
  explicit Value(ConstructorAccessTag, language::gc::Pool& pool, const Type& t);

  // TODO(2025-05-30, trivial?): This should be deleted. Make it impossible
  // to build uninitialized objects. The other static factory methods should
  // call a constructor that receives the values for `value_` and
  // `expand_callback_` (and `pool_` and `type_`).
  static language::gc::Root<Value> New(language::gc::Pool& pool, const Type&);
  static language::gc::Root<Value> NewVoid(language::gc::Pool& pool);
  static language::gc::Root<Value> NewBool(language::gc::Pool& pool,
                                           bool value);
  static language::gc::Root<Value> NewNumber(language::gc::Pool& pool,
                                             math::numbers::Number value);
  static language::gc::Root<Value> NewString(
      language::gc::Pool& pool, language::lazy_string::LazyString value);
  static language::gc::Root<Value> NewSymbol(language::gc::Pool& pool,
                                             Identifier value);
  static language::gc::Root<Value> NewObject(
      language::gc::Pool& pool, types::ObjectName name,
      language::NonNull<std::shared_ptr<void>> value,
      ExpandCallback expand_callback = [] {
        return std::vector<
            language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>();
      });
  static language::gc::Root<Value> NewFunction(
      language::gc::Pool& pool, PurityType purity_type, Type output,
      std::vector<Type> inputs, Callback callback,
      ExpandCallback expand_callback = []() {
        return std::vector<
            language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>();
      });

  // Convenience wrapper.
  static language::gc::Root<Value> NewFunction(
      language::gc::Pool& pool, PurityType purity_type, Type output,
      std::vector<Type> inputs,
      std::function<
          language::gc::Root<Value>(std::vector<language::gc::Root<Value>>)>
          callback);

  const Type& type() const;

  bool IsVoid() const;
  bool IsBool() const;
  bool IsNumber() const;
  bool IsString() const;
  bool IsSymbol() const;
  bool IsFunction() const;
  bool IsObject() const;

  bool get_bool() const;
  language::ValueOrError<int32_t> get_int32() const;
  language::ValueOrError<int64_t> get_int() const;
  const math::numbers::Number& get_number() const;
  const language::lazy_string::LazyString& get_string() const;
  const Identifier& get_symbol() const;

  template <typename T>
  language::NonNull<std::shared_ptr<T>> get_user_value(
      const types::ObjectName& expected_type) const {
    CHECK_EQ(std::get<types::ObjectName>(type_), expected_type);
    return language::NonNull<std::shared_ptr<T>>::UnsafeStaticCast(
        std::get<ObjectInstance>(value_).value);
  }

  template <typename T>
  language::NonNull<std::shared_ptr<T>> get_user_value(
      const Type& expected_type) const {
    CHECK(std::holds_alternative<ObjectInstance>(value_))
        << "Invalid call to get_user_value, expected type: " << expected_type
        << ", index was: " << value_.index();
    return get_user_value<T>(std::get<types::ObjectName>(type_));
  }

  futures::ValueOrError<language::gc::Root<Value>> RunFunction(
      std::vector<language::gc::Root<Value>> arguments, Trampoline& trampoline);

  // This is similar to `get_double`, but can deal with type conversion from
  // integer.
  language::ValueOrError<double> ToDouble() const;

  std::vector<language::NonNull<std::shared_ptr<language::gc::ObjectMetadata>>>
  Expand() const;
};

std::ostream& operator<<(std::ostream& os, const Value& value);

}  // namespace afc::vm
#endif  // __AFC_VM_PUBLIC_VALUE_H__
