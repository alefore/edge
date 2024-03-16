#include "src/language/gc.h"
#include "src/math/numbers.h"
#include "src/tests/tests.h"
#include "src/vm/default_environment.h"
#include "src/vm/expression.h"

namespace gc = afc::language::gc;
using afc::language::ValueOrDie;
using afc::language::ValueOrError;
using afc::language::lazy_string::LazyString;
using afc::math::numbers::Number;

namespace afc::vm {
namespace {
using PromotionCallback =
    std::function<gc::Root<Value>(gc::Pool&, gc::Root<Value>)>;

const bool tests_registration = tests::Register(
    L"GetImplicitPromotion",
    {
        {.name = L"NoPromotion",
         .callback =
             [] {
               CHECK(GetImplicitPromotion(types::String{}, types::Number{}) ==
                     nullptr);
             }},
        {.name = L"NumberToNumber",
         .callback =
             [] {
               gc::Pool pool({});
               PromotionCallback callback =
                   GetImplicitPromotion(types::Number{}, types::Number{});
               CHECK(callback != nullptr);
               gc::Root<Value> output =
                   callback(pool, Value::NewNumber(pool, Number::FromInt64(5)));
               ValueOrError<std::wstring> output_str =
                   output.ptr()->get_number().ToString(2);
               LOG(INFO) << "Output str: " << output_str;
               CHECK(ValueOrDie(std::move(output_str)) == L"5");
             }},
        {.name = L"FunctionNoPromotion",
         .callback =
             [] {
               // No promotion: the return type doesn't match (int and string).
               std::vector<Type> inputs = {types::String(), types::Bool()};
               CHECK(GetImplicitPromotion(
                         types::Function{.output = Type{types::Number{}},
                                         .inputs = inputs},
                         types::Function{.output = Type{types::String{}},
                                         .inputs = inputs}) == nullptr);
             }},
        {.name = L"FunctionReturnType",
         .callback =
             [] {
               gc::Pool pool({});
               std::vector<Type> inputs = {types::String(), types::Bool()};
               gc::Root<Value> promoted_function = GetImplicitPromotion(
                   types::Function{.output = Type{types::Number{}},
                                   .inputs = inputs},
                   types::Function{.output = Type{types::Number{}},
                                   .inputs = inputs})(
                   pool, vm::NewCallback(pool, PurityType::kUnknown,
                                         [](std::wstring s, bool b) {
                                           CHECK(s == L"alejo");
                                           CHECK_EQ(b, true);
                                           return Number::FromInt64(4);
                                         }));
               Trampoline trampoline(Trampoline::Options{
                   .pool = pool,
                   .environment = NewDefaultEnvironment(pool),
                   .yield_callback = nullptr});
               futures::ValueOrError<gc::Root<Value>> output =
                   promoted_function.ptr()->LockCallback().ptr()->value(
                       {Value::NewString(pool, LazyString{L"alejo"}),
                        Value::NewBool(pool, true)},
                       trampoline);
               CHECK(ValueOrDie(output.Get().value())
                         .ptr()
                         ->get_number()
                         .ToString(2) == L"4");
             }},
    });
}  // namespace
}  // namespace afc::vm
