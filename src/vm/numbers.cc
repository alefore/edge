#include "src/vm/numbers.h"

#include <tgmath.h>

#include "src/vm/callbacks.h"
#include "src/vm/environment.h"

using afc::language::Success;
using afc::math::numbers::FromDouble;
using afc::math::numbers::Number;
using afc::math::numbers::ToDouble;

namespace afc::vm {
namespace gc = language::gc;

void RegisterNumberFunctions(gc::Pool& pool, Environment& environment) {
  auto add = [&](std::wstring name, std::function<double(double)> func) {
    environment.Define(
        name, NewCallback(pool, PurityType::kPure,
                          [func](double input) { return func(input); }));
  };
  add(L"log", log);
  add(L"log2", log2);
  add(L"log10", log10);
  add(L"exp", exp);
  add(L"exp2", exp2);
  environment.Define(L"pow",
                     NewCallback(pool, PurityType::kPure,
                                 std::function<double(double, double)>(pow)));
}
}  // namespace afc::vm