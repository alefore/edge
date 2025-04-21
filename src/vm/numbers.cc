#include "src/vm/numbers.h"

#include <tgmath.h>

#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"

using afc::language::Success;
using afc::language::lazy_string::LazyString;
using afc::language::lazy_string::NonEmptySingleLine;
using afc::language::lazy_string::SingleLine;
using afc::math::numbers::Number;

namespace afc::vm {
namespace gc = language::gc;

void RegisterNumberFunctions(gc::Pool& pool, Environment& environment) {
  gc::Root<ObjectType> number_type = ObjectType::New(pool, types::Number{});
  number_type.ptr()->AddField(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"tostring")},
      NewCallback(pool, kPurityTypePure, [](Number value) {
        return futures::Past(value.ToString(5));
      }).ptr());
  environment.DefineType(number_type.ptr());

  auto add = [&](Identifier name, std::function<double(double)> func) {
    environment.Define(
        name, NewCallback(pool, PurityType{},
                          [func](double input) { return func(input); }));
  };
  add(Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"log")}, log);
  add(Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"log2")}, log2);
  add(Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"log10")}, log10);
  add(Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"exp")}, exp);
  add(Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"exp2")}, exp2);
  environment.Define(Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"pow")},
                     NewCallback(pool, PurityType{},
                                 std::function<double(double, double)>(pow)));
  environment.Define(
      Identifier{NON_EMPTY_SINGLE_LINE_CONSTANT(L"number")},
      NewCallback(pool, PurityType{}, [] { return Number::FromSizeT(0); }));
}
}  // namespace afc::vm
