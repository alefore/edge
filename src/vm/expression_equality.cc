#include "src/vm/expression_equality.h"

#include <glog/logging.h>

#include <utility>  // For std::move

#include "src/language/error/value_or_error.h"
#include "src/language/lazy_string/lazy_string.h"
#include "src/math/numbers.h"
#include "src/vm/binary_operator.h"
#include "src/vm/compilation.h"  // Full definition needed here
#include "src/vm/delegating_expression.h"
#include "src/vm/expression.h"  // Full definition needed here
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

ValueOrError<gc::Root<Expression>> ExpressionEquals(
    Compilation& compilation, std::optional<gc::Ptr<Expression>> a,
    std::optional<gc::Ptr<Expression>> b) {
  if (a == std::nullopt || b == std::nullopt) {
    return Error{LazyString{L"Missing inputs."}};
  } else if (a.value()->IsString() && b.value()->IsString()) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a).value(), std::move(b).value(), types::Bool{},
        [](gc::Pool& pool, const Value& a_str, const Value& b_str) {
          return Value::NewBool(pool, a_str.get_string() == b_str.get_string());
        }));
  } else if (a.value()->IsNumber() && b.value()->IsNumber()) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a).value(), std::move(b).value(), types::Bool{},
        [precision = compilation.numbers_precision](
            gc::Pool& pool, const Value& a_value,
            const Value& b_value) -> ValueOrError<gc::Root<Value>> {
          return Value::NewBool(pool,
                                a_value.get_number() == b_value.get_number());
        }));
  } else if (a.value()->IsBool() && b.value()->IsBool()) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a).value(), std::move(b).value(), types::Bool{},
        [](gc::Pool& pool, const Value& a_bool, const Value& b_bool) {
          return Value::NewBool(pool, a_bool.get_bool() == b_bool.get_bool());
        }));
  } else if (a.value()->Types().front() == b.value()->Types().front() &&
             std::holds_alternative<types::ObjectName>(
                 a.value()->Types().front())) {
    return compilation.RegisterErrors(BinaryOperator::New(
        std::move(a).value(), std::move(b).value(), types::Bool{},
        [](gc::Pool& pool, const Value& a_value, const Value& b_value) {
          return Value::NewBool(
              pool,
              a_value.get_user_value<void>(a_value.type()).get_shared() ==
                  b_value.get_user_value<void>(b_value.type()).get_shared());
        }));
  } else {
    return compilation.AddError(
        Error{LazyString{L"Unable to compare types: "} +
              TypesToString(a.value()->Types()) + LazyString{L" == "} +
              TypesToString(b.value()->Types()) + LazyString{L"."}});
  }
}

}  // namespace afc::vm
