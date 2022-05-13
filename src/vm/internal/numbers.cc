#include "src/vm/internal/numbers.h"

#include <tgmath.h>

#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"

namespace afc::vm {
namespace gc = language::gc;

void RegisterNumberFunctions(gc::Pool& pool, Environment& environment) {
  environment.Define(L"log",
                     NewCallback(pool, std::function<double(double)>(log),
                                 VMType::PurityType::kPure));
  environment.Define(L"log2",
                     NewCallback(pool, std::function<double(double)>(log2),
                                 VMType::PurityType::kPure));
  environment.Define(L"log10",
                     NewCallback(pool, std::function<double(double)>(log10),
                                 VMType::PurityType::kPure));
  environment.Define(L"exp",
                     NewCallback(pool, std::function<double(double)>(exp),
                                 VMType::PurityType::kPure));
  environment.Define(
      L"pow", NewCallback(pool, std::function<double(double, double)>(pow),
                          VMType::PurityType::kPure));
}
}  // namespace afc::vm
