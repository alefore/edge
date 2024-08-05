#include "src/vm/numbers.h"

#include <tgmath.h>

#include "src/language/lazy_string/lazy_string.h"
#include "src/vm/callbacks.h"
#include "src/vm/environment.h"

using afc::language::Success;
using afc::language::lazy_string::LazyString;
using afc::math::numbers::Number;

namespace afc::vm {
namespace gc = language::gc;

void RegisterNumberFunctions(gc::Pool& pool, Environment& environment) {
  auto add = [&](Identifier name, std::function<double(double)> func) {
    environment.Define(
        name, NewCallback(pool, PurityType{},
                          [func](double input) { return func(input); }));
  };
  add(Identifier{LazyString{L"log"}}, log);
  add(Identifier{LazyString{L"log2"}}, log2);
  add(Identifier{LazyString{L"log10"}}, log10);
  add(Identifier{LazyString{L"exp"}}, exp);
  add(Identifier{LazyString{L"exp2"}}, exp2);
  environment.Define(Identifier{LazyString{L"pow"}},
                     NewCallback(pool, PurityType{},
                                 std::function<double(double, double)>(pow)));
}
}  // namespace afc::vm
