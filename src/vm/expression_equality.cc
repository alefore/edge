#include "src/vm/expression_equality.h"

#include <glog/logging.h>

#include <utility>  // For std::move

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/math/numbers.h"
#include "src/vm/binary_operator.h"
#include "src/vm/compilation.h"  // Full definition needed here
#include "src/vm/expression.h"   // Full definition needed here
#include "src/vm/types.h"
#include "src/vm/value.h"

namespace gc = afc::language::gc;

using afc::language::Error;
using afc::language::NonNull;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::vm::TypesToString;
using afc::vm::Value;
using afc::vm::types::Bool;
using afc::vm::types::ObjectName;

namespace afc::vm {

std::unique_ptr<Expression> ExpressionEquals(Compilation& compilation,
                                             std::unique_ptr<Expression> a,
                                             std::unique_ptr<Expression> b) {
  if (a == nullptr || b == nullptr) {
    return nullptr;
  } else if (a->IsString() && b->IsString()) {
    return std::unique_ptr<Expression>(
        std::move(ToUniquePtr(compilation.RegisterErrors(BinaryOperator::New(
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
            types::Bool{},
            [](gc::Pool& pool, const Value& a_str, const Value& b_str) {
              return Value::NewBool(pool,
                                    a_str.get_string() == b_str.get_string());
            })))));
  } else if (a->IsNumber() && b->IsNumber()) {
    return std::unique_ptr<Expression>(
        std::move(ToUniquePtr(compilation.RegisterErrors(BinaryOperator::New(
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
            types::Bool{},
            [precision = compilation.numbers_precision](
                gc::Pool& pool, const Value& a_value,
                const Value& b_value) -> ValueOrError<gc::Root<Value>> {
              return Value::NewBool(
                  pool, a_value.get_number() == b_value.get_number());
            })))));
  } else if (a->IsBool() && b->IsBool()) {
    return std::unique_ptr<Expression>(
        std::move(ToUniquePtr(compilation.RegisterErrors(BinaryOperator::New(
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
            types::Bool{},
            [](gc::Pool& pool, const Value& a_bool, const Value& b_bool) {
              return Value::NewBool(pool,
                                    a_bool.get_bool() == b_bool.get_bool());
            })))));
  } else if (a->Types().front() == b->Types().front() &&
             std::holds_alternative<types::ObjectName>(a->Types().front())) {
    return std::unique_ptr<Expression>(
        std::move(ToUniquePtr(compilation.RegisterErrors(BinaryOperator::New(
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(a)),
            NonNull<std::unique_ptr<Expression>>::Unsafe(std::move(b)),
            types::Bool{},
            [](gc::Pool& pool, const Value& a_value, const Value& b_value) {
              return Value::NewBool(
                  pool,
                  a_value.get_user_value<void>(a_value.type()).get_shared() ==
                      b_value.get_user_value<void>(b_value.type())
                          .get_shared());
            })))));
  } else {
    compilation.AddError(Error{LazyString{L"Unable to compare types: "} +
                               TypesToString(a->Types()) + LazyString{L" == "} +
                               TypesToString(b->Types()) + LazyString{L"."}});
    return nullptr;
  }
}

}  // namespace afc::vm
