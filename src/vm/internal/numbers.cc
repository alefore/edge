#include "src/vm/internal/numbers.h"

#include <tgmath.h>

#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"

using afc::language::Success;
using afc::language::numbers::FromDouble;
using afc::language::numbers::Number;
using afc::language::numbers::ToDouble;

namespace afc::vm {
namespace gc = language::gc;

void RegisterNumberFunctions(gc::Pool& pool, Environment& environment) {
  auto add = [&](std::wstring name, std::function<double(double)> func) {
    environment.Define(
        name, NewCallback(pool, PurityType::kPure, [func](double input) {
          return FromDouble(func(input));
        }));
  };
  add(L"log", log);
  add(L"log2", log2);
  add(L"log10", log10);
  add(L"exp", exp);
  add(L"exp2", exp2);
  environment.Define(
      L"pow",
      NewCallback(
          pool, PurityType::kPure,
          [](Number base_number,
             Number exponent_number) -> futures::ValueOrError<Number> {
            FUTURES_ASSIGN_OR_RETURN(double base, ToDouble(base_number));
            FUTURES_ASSIGN_OR_RETURN(double exponent,
                                     ToDouble(exponent_number));
            LOG(INFO) << "Compute pow: " << base << ", " << exponent;
            return futures::Past(Success(FromDouble(pow(base, exponent))));
          }));
}
}  // namespace afc::vm
