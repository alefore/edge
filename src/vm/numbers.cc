#include "src/vm/numbers.h"

#include <tgmath.h>

#include "src/vm/callbacks.h"
#include "src/vm/environment.h"

using afc::language::Success;
using afc::math::numbers::Number;

namespace afc::vm {
namespace gc = language::gc;

void RegisterNumberFunctions(gc::Pool& pool, Environment& environment) {
  auto add = [&](Identifier name, std::function<double(double)> func) {
    environment.Define(
        name, NewCallback(pool, PurityType{},
                          [func](double input) { return func(input); }));
  };
  add(Identifier(L"log"), log);
  add(Identifier(L"log2"), log2);
  add(Identifier(L"log10"), log10);
  add(Identifier(L"exp"), exp);
  add(Identifier(L"exp2"), exp2);
  environment.Define(Identifier(L"pow"),
                     NewCallback(pool, PurityType{},
                                 std::function<double(double, double)>(pow)));
}
}  // namespace afc::vm
