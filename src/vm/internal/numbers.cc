#include "src/vm/internal/numbers.h"

#include <tgmath.h>

#include "src/vm/public/callbacks.h"
#include "src/vm/public/environment.h"

namespace afc::vm {
void RegisterNumberFunctions(Environment* environment) {
  environment->Define(L"log",
                      vm::NewCallback(std::function<double(double)>(log),
                                      VMType::PurityType::kPure));
  environment->Define(L"log2",
                      vm::NewCallback(std::function<double(double)>(log2),
                                      VMType::PurityType::kPure));
  environment->Define(L"log10",
                      vm::NewCallback(std::function<double(double)>(log10),
                                      VMType::PurityType::kPure));
  environment->Define(L"exp",
                      vm::NewCallback(std::function<double(double)>(exp),
                                      VMType::PurityType::kPure));
  environment->Define(
      L"pow", vm::NewCallback(std::function<double(double, double)>(pow),
                              VMType::PurityType::kPure));
}
}  // namespace afc::vm
